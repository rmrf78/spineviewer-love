#include "spine_scene.h"
#include "../core/viewer_controller.h"
#include <QFile>
#include <QMatrix4x4>
#include <QQuickWindow>
#include <QMouseEvent>
#include <QWheelEvent>
#include <rhi/qrhi.h>
#include <rhi/qshader.h>
#include <rhi/qrhi_platform.h>
#include "../core/live2d_bridge.h"
#include <array>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <unordered_map>

namespace slqt {
namespace {
QShader shader(const char* resource) {
    QFile f(QString::fromUtf8(resource));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QShader::fromSerialized(f.readAll());
}
struct Vertex { float x,y,r,g,b,a,u,v; };
struct DrawResources {
    std::unique_ptr<QRhiBuffer> vertices,indices,uniform;
    std::unique_ptr<QRhiShaderResourceBindings> bindings;
    std::unique_ptr<QRhiGraphicsPipeline> pipeline;
    QRhiTexture* texture = nullptr;
    QRhiTexture* mask = nullptr;
    int count = 0;
    int key = -1;
    QRhiRenderPassDescriptor* pass = nullptr;
};
struct MaskResources {
    std::unique_ptr<QRhiTexture> texture;
    std::unique_ptr<QRhiRenderPassDescriptor> pass;
    std::unique_ptr<QRhiTextureRenderTarget> target;
    std::vector<std::unique_ptr<DrawResources>> draws;
};
class SceneRenderer final : public QQuickRhiItemRenderer {
    QRhi* m_rhi = nullptr;
    std::shared_ptr<const SceneSnapshot> m_frame;
    std::unordered_map<SlTextureId,std::unique_ptr<QRhiTexture>> m_textures;
    std::unique_ptr<QRhiSampler> m_sampler;
    std::unique_ptr<QRhiTexture> m_white;
    std::vector<std::unique_ptr<DrawResources>> m_draws;
    std::vector<std::unique_ptr<MaskResources>> m_masks;
    QShader m_vs, m_fs;
    quint64 m_revision = ~quint64(0);
    quint64 m_sourceGeneration=~quint64(0);
    bool m_whiteUploaded = false;
    std::shared_ptr<Live2DBridge> m_live2d;
    std::unique_ptr<QRhiTexture> m_liveTexture;
    std::unique_ptr<DrawResources> m_liveDraw;
    quint64 m_liveNative = 0;
    double m_lastLiveTime = 0;
    std::shared_ptr<const SceneSnapshot> m_lastLiveFrame;
    QRhiReadbackResult m_readback;
    std::shared_ptr<SceneCaptureRequest> m_pendingCapture;

public:
    ~SceneRenderer() override {
        if(m_pendingCapture&&!m_pendingCapture->completed.load(std::memory_order_acquire)){
            if(m_rhi)m_rhi->finish();
            if(!m_pendingCapture->completed.load(std::memory_order_acquire)){
                m_pendingCapture->error=QStringLiteral("The render surface closed during capture.");
                m_pendingCapture->completed.store(true,std::memory_order_release);
            }
        }
        m_liveDraw.reset(); m_liveTexture.reset();
        if (m_live2d) m_live2d->shutdown();
    }
private:

    void initialize(QRhiCommandBuffer*) override {
        if (m_rhi == rhi()) return;
        m_liveDraw.reset();m_liveTexture.reset();m_liveNative=0;
        m_lastLiveFrame.reset();
        if(m_live2d)m_live2d->shutdown();
        m_draws.clear(); m_masks.clear(); m_textures.clear();
        m_sampler.reset(); m_white.reset();
        m_rhi = rhi(); m_revision = ~quint64(0); m_whiteUploaded=false;
        m_sampler.reset(rhi()->newSampler(QRhiSampler::Linear,QRhiSampler::Linear,QRhiSampler::None,
            QRhiSampler::ClampToEdge,QRhiSampler::ClampToEdge));
        m_sampler->create();
        m_white.reset(rhi()->newTexture(QRhiTexture::RGBA8,QSize(1,1),1));
        m_white->create();
        m_vs=shader(":/shaders/spine.vert.qsb");
        m_fs=shader(":/shaders/spine.frag.qsb");
    }
    void synchronize(QQuickRhiItem* item) override {
        auto* scene=static_cast<SpineScene*>(item);
        if(m_sourceGeneration!=scene->sourceGeneration()){m_sourceGeneration=scene->sourceGeneration();m_revision=~quint64(0);}
        m_frame=scene->snapshot();
    }
    static QRhiGraphicsPipeline::TargetBlend blend(SlBlendMode mode,bool pma) {
        using P=QRhiGraphicsPipeline;
        P::TargetBlend b;
        b.enable=true; b.srcColor=pma?P::One:P::SrcAlpha; b.dstColor=P::OneMinusSrcAlpha;
        b.srcAlpha=P::One; b.dstAlpha=P::OneMinusSrcAlpha;
        if(mode==SlBlendMode::Additive){b.dstColor=P::One;b.dstAlpha=P::One;}
        else if(mode==SlBlendMode::Multiply){b.srcColor=P::DstColor;}
        else if(mode==SlBlendMode::Screen){b.srcColor=P::One;b.dstColor=P::OneMinusSrcColor;}
        return b;
    }
    QRhiTexture* texture(SlTextureId id) {
        const auto it=m_textures.find(id);
        return it==m_textures.end()?nullptr:it->second.get();
    }
    bool prepare(DrawResources& d,const std::vector<SlVertex2D>& vertices,
        const std::vector<unsigned short>& indices,QRhiTexture* tex,QRhiTexture* mask,
        SlBlendMode mode,bool pma,int maskMode,QRhiRenderTarget* target,QRhiResourceUpdateBatch* batch) {
        d.count=0;
        if(!tex||vertices.empty()||indices.empty())return false;
        const int vbBytes=int(vertices.size()*sizeof(Vertex));
        const int ibBytes=int(indices.size()*sizeof(unsigned short));
        if(!d.vertices||d.vertices->size()<vbBytes){
            d.vertices.reset(rhi()->newBuffer(QRhiBuffer::Dynamic,QRhiBuffer::VertexBuffer,vbBytes));
            if(!d.vertices->create())return false;
        }
        if(!d.indices||d.indices->size()<ibBytes){
            d.indices.reset(rhi()->newBuffer(QRhiBuffer::Dynamic,QRhiBuffer::IndexBuffer,ibBytes));
            if(!d.indices->create())return false;
        }
        if(!d.uniform){d.uniform.reset(rhi()->newBuffer(QRhiBuffer::Dynamic,QRhiBuffer::UniformBuffer,80));if(!d.uniform->create())return false;}
        std::vector<Vertex> packed;packed.reserve(vertices.size());
        for(const auto& v:vertices)packed.push_back({v.pos.x,v.pos.y,v.color.r,v.color.g,v.color.b,v.color.a,v.uv.x,v.uv.y});
        batch->updateDynamicBuffer(d.vertices.get(),0,vbBytes,packed.data());
        batch->updateDynamicBuffer(d.indices.get(),0,ibBytes,indices.data());
        QMatrix4x4 projection;projection.ortho(0.f,float(m_frame->size.width()),float(m_frame->size.height()),0.f,-1.f,1.f);
        const QMatrix4x4 corrected=rhi()->clipSpaceCorrMatrix()*projection;
        std::array<float,20> params{};
        std::memcpy(params.data(),corrected.constData(),64);
        params[16]=float(m_frame->size.width());params[17]=float(m_frame->size.height());
        params[18]=float(maskMode);params[19]=rhi()->isYUpInFramebuffer()?1.f:0.f;
        batch->updateDynamicBuffer(d.uniform.get(),0,80,params.data());
        if(!mask)mask=m_white.get();
        if(!d.bindings||d.texture!=tex||d.mask!=mask){
            d.bindings.reset(rhi()->newShaderResourceBindings());
            d.bindings->setBindings({
                QRhiShaderResourceBinding::uniformBuffer(0,QRhiShaderResourceBinding::VertexStage|QRhiShaderResourceBinding::FragmentStage,d.uniform.get()),
                QRhiShaderResourceBinding::sampledTexture(1,QRhiShaderResourceBinding::FragmentStage,tex,m_sampler.get()),
                QRhiShaderResourceBinding::sampledTexture(2,QRhiShaderResourceBinding::FragmentStage,mask,m_sampler.get())});
            if(!d.bindings->create())return false;
            d.texture=tex;d.mask=mask;
        }
        const int key=int(mode)*2+int(pma);
        if(!d.pipeline||d.key!=key||d.pass!=target->renderPassDescriptor()){
            d.pipeline.reset(rhi()->newGraphicsPipeline());
            d.pipeline->setShaderStages({{QRhiShaderStage::Vertex,m_vs},{QRhiShaderStage::Fragment,m_fs}});
            QRhiVertexInputLayout layout;
            layout.setBindings({QRhiVertexInputBinding(sizeof(Vertex))});
            layout.setAttributes({{0,0,QRhiVertexInputAttribute::Float2,0},{0,1,QRhiVertexInputAttribute::Float4,8},{0,2,QRhiVertexInputAttribute::Float2,24}});
            d.pipeline->setVertexInputLayout(layout);
            d.pipeline->setShaderResourceBindings(d.bindings.get());
            d.pipeline->setRenderPassDescriptor(target->renderPassDescriptor());
            d.pipeline->setSampleCount(target->sampleCount());
            d.pipeline->setTargetBlends({blend(mode,pma)});
            if(!d.pipeline->create())return false;
            d.key=key;d.pass=target->renderPassDescriptor();
        }
        d.count=int(indices.size());return true;
    }
    void draw(QRhiCommandBuffer* cb,DrawResources& d) {
        if(d.count<=0)return;
        cb->setGraphicsPipeline(d.pipeline.get());
        cb->setShaderResources(d.bindings.get());
        const QRhiCommandBuffer::VertexInput input(d.vertices.get(),0);
        cb->setVertexInput(0,1,&input,d.indices.get(),0,QRhiCommandBuffer::IndexUInt16);
        cb->drawIndexed(d.count);
    }
    void render(QRhiCommandBuffer* cb) override {
        bool liveReady=false;
        if(m_frame&&m_frame->live2d){
            m_live2d=m_frame->live2d;
#if defined(Q_OS_WIN)
            if(rhi()->backend()==QRhi::D3D11){
                const auto* native=static_cast<const QRhiD3D11NativeHandles*>(rhi()->nativeHandles());
                cb->beginExternal();
                const bool ready=m_live2d->initialize(static_cast<ID3D11Device*>(native->dev),static_cast<ID3D11DeviceContext*>(native->context),m_frame->live2dShaderPath);
                const float dt=m_frame->capture?0.f:float(std::clamp(m_frame->animationTime-m_lastLiveTime,0.0,0.1));
                if(ready){
                    const bool alreadyRendered=m_lastLiveFrame==m_frame&&m_live2d->nativeTexture()
                        &&m_live2d->textureSize()==m_frame->size;
                    liveReady=alreadyRendered||m_live2d->render(dt,m_frame->size.width(),m_frame->size.height(),0,-m_frame->titleHeight/std::max(1,m_frame->size.height()));
                    if(liveReady)m_lastLiveFrame=m_frame;
                }
                m_lastLiveTime=m_frame->animationTime;
                cb->endExternal();
                if(liveReady){
                    const quint64 handle=quint64(reinterpret_cast<quintptr>(m_live2d->nativeTexture()));
                    if(!m_liveTexture||m_liveNative!=handle||m_liveTexture->pixelSize()!=m_live2d->textureSize()){
                        m_liveDraw.reset();m_liveTexture.reset();
                        m_liveTexture.reset(rhi()->newTexture(QRhiTexture::RGBA8,m_live2d->textureSize(),1));
                        liveReady=m_liveTexture->createFrom({handle,0});m_liveNative=handle;
                        if(!liveReady){m_liveTexture.reset();m_liveNative=0;}
                    }
                }
            }else m_live2d->initialize(nullptr,nullptr,{});
#else
            m_live2d->initialize(nullptr,nullptr,{});
#endif
        }else if(m_frame){
            if(m_live2d){cb->beginExternal();m_live2d->processPendingCommands();cb->endExternal();}
            m_lastLiveTime=m_frame->animationTime;
        }
        auto* batch=rhi()->nextResourceUpdateBatch();
        if(!m_whiteUploaded){QImage img(1,1,QImage::Format_RGBA8888);img.fill(Qt::white);batch->uploadTexture(m_white.get(),img);m_whiteUploaded=true;}
        if(!m_frame||m_frame->size.isEmpty()||!m_vs.isValid()||!m_fs.isValid()){
            cb->beginPass(renderTarget(),Qt::black,{1,0},batch);cb->endPass();return;
        }
        if(m_revision!=m_frame->textureRevision){

            m_draws.clear();m_masks.clear();
            for(auto it=m_textures.begin();it!=m_textures.end();){if(!m_frame->textures.contains(it->first))it=m_textures.erase(it);else ++it;}
            for(auto it=m_frame->textures.cbegin();it!=m_frame->textures.cend();++it){
                if(m_textures.count(it.key()))continue;
                auto t=std::unique_ptr<QRhiTexture>(rhi()->newTexture(QRhiTexture::RGBA8,it.value().size(),1));
                if(t->create()){batch->uploadTexture(t.get(),it.value());m_textures.emplace(it.key(),std::move(t));}
            }
            m_revision=m_frame->textureRevision;
        }
        const auto count=m_frame->draws.size();m_draws.resize(count);m_masks.resize(count);
        for(size_t i=0;i<count;++i){
            const auto& c=m_frame->draws[i];
            if(!m_draws[i])m_draws[i]=std::make_unique<DrawResources>();
            QRhiTexture* maskTexture=nullptr;
            if(!c.masks.empty()){
                auto& mask=m_masks[i];
                if(!mask||mask->texture->pixelSize()!=renderTarget()->pixelSize()){
                    mask=std::make_unique<MaskResources>();
                    mask->texture.reset(rhi()->newTexture(QRhiTexture::RGBA8,renderTarget()->pixelSize(),1,QRhiTexture::RenderTarget));
                    mask->texture->create();
                    mask->target.reset(rhi()->newTextureRenderTarget(QRhiTextureRenderTargetDescription(mask->texture.get())));
                    mask->pass.reset(mask->target->newCompatibleRenderPassDescriptor());mask->target->setRenderPassDescriptor(mask->pass.get());mask->target->create();
                }
                maskTexture=mask->texture.get();mask->draws.resize(c.masks.size());
                for(size_t j=0;j<c.masks.size();++j){
                    const auto& m=c.masks[j];if(!mask->draws[j])mask->draws[j]=std::make_unique<DrawResources>();
                    prepare(*mask->draws[j],m.vertices,m.indices,texture(SlTextureId(m.textureId)),nullptr,SlBlendMode::Normal,m.premultipliedAlpha,0,mask->target.get(),batch);
                }
            }else m_masks[i].reset();
            prepare(*m_draws[i],c.vertices,c.indices,texture(SlTextureId(c.textureId)),maskTexture,c.blendMode,c.premultipliedAlpha,maskTexture?(c.invertedMask?2:1):0,renderTarget(),batch);
        }
        if(liveReady&&m_liveTexture){
            if(!m_liveDraw)m_liveDraw=std::make_unique<DrawResources>();
            const float w=float(m_frame->size.width()),h=float(m_frame->size.height());
            std::vector<SlVertex2D> quad(4);
            quad[0].pos={0,0,0};quad[0].uv={0,0};quad[1].pos={w,0,0};quad[1].uv={1,0};
            quad[2].pos={w,h,0};quad[2].uv={1,1};quad[3].pos={0,h,0};quad[3].uv={0,1};
            prepare(*m_liveDraw,quad,{0,1,2,2,3,0},m_liveTexture.get(),nullptr,SlBlendMode::Normal,true,0,renderTarget(),batch);
        }
        cb->resourceUpdate(batch);
        for(auto& mask:m_masks)if(mask){
            cb->beginPass(mask->target.get(),Qt::transparent,{1,0});
            const auto sz=mask->target->pixelSize();cb->setViewport({0,0,float(sz.width()),float(sz.height())});
            for(auto& d:mask->draws)draw(cb,*d);cb->endPass();
        }
        cb->beginPass(renderTarget(),m_frame->clearColor,{1,0});
        const auto sz=renderTarget()->pixelSize();cb->setViewport({0,0,float(sz.width()),float(sz.height())});
        for(auto& d:m_draws)draw(cb,*d);
        if(liveReady&&m_liveDraw)draw(cb,*m_liveDraw);
        cb->endPass();
        if(m_frame->capture&&(!m_pendingCapture||m_pendingCapture->completed.load(std::memory_order_acquire))&&!m_frame->capture->submitted.exchange(true)){
            m_pendingCapture=m_frame->capture;
            auto request=m_pendingCapture;
            const bool flip=rhi()->isYUpInFramebuffer();
            m_readback.completed=[this,request,flip]{
                const QSize size=m_readback.pixelSize;
                if(size.isEmpty()||m_readback.data.size()<qsizetype(size.width())*size.height()*4){
                    request->error=QStringLiteral("GPU returned an incomplete stage image.");
                }else{
                    auto image=QImage(reinterpret_cast<const uchar*>(m_readback.data.constData()),size.width(),size.height(),size.width()*4,QImage::Format_RGBA8888_Premultiplied).copy();
                    if(flip)image=image.mirrored(false,true);
                    request->image=std::move(image);
                }
                request->completed.store(true,std::memory_order_release);
            };
            auto* readbackBatch=rhi()->nextResourceUpdateBatch();
            readbackBatch->readBackTexture(QRhiReadbackDescription(resolveTexture()?resolveTexture():colorTexture()),&m_readback);
            cb->resourceUpdate(readbackBatch);
        }
        if(m_pendingCapture&&!m_pendingCapture->completed.load(std::memory_order_acquire))update();
    }
};
}
SpineScene::SpineScene(QQuickItem* parent):QQuickRhiItem(parent){
    setAcceptedMouseButtons(Qt::AllButtons);setAcceptHoverEvents(true);setAlphaBlending(true);
    connect(this,&QQuickItem::windowChanged,this,[this]{updateViewport();});
}
void SpineScene::setController(ViewerController* controller){
    if(m_controller==controller)return;
    if(m_controller)disconnect(m_controller,nullptr,this,nullptr);
    m_controller=controller;
    ++m_sourceGeneration;
    if(m_controller)connect(m_controller,&ViewerController::frameChanged,this,&QQuickItem::update);
    updateViewport();emit controllerChanged();update();
}
std::shared_ptr<const SceneSnapshot> SpineScene::snapshot() const{return m_controller?m_controller->snapshot():nullptr;}
QQuickRhiItemRenderer* SpineScene::createRenderer(){return new SceneRenderer;}
void SpineScene::updateViewport(){

    if(!window())return;
    const qreal dpr=window()->devicePixelRatio();

    const int w=std::max(1,int(std::ceil(width()*dpr))),h=std::max(1,int(std::ceil(height()*dpr)));
    setFixedColorBufferWidth(w);setFixedColorBufferHeight(h);
    if(m_controller)m_controller->setViewport(QSizeF(width(),height()),dpr);
}
void SpineScene::geometryChange(const QRectF& now,const QRectF& before){QQuickRhiItem::geometryChange(now,before);updateViewport();}
void SpineScene::itemChange(ItemChange change,const ItemChangeData& data){
    QQuickRhiItem::itemChange(change,data);

    if(change==ItemSceneChange&&window())updateViewport();

    if(change==ItemDevicePixelRatioHasChanged)updateViewport();
}
void SpineScene::mousePressEvent(QMouseEvent* e){forceActiveFocus(Qt::MouseFocusReason);if(m_controller)m_controller->pointerPress(e->position(),e->button(),e->modifiers());e->accept();}
void SpineScene::mouseMoveEvent(QMouseEvent* e){if(m_controller)m_controller->pointerMove(e->position(),e->buttons(),e->modifiers());e->accept();}
void SpineScene::mouseReleaseEvent(QMouseEvent* e){if(m_controller)m_controller->pointerRelease(e->position(),e->button(),e->modifiers());e->accept();}
void SpineScene::wheelEvent(QWheelEvent* e){if(m_controller)m_controller->wheel(e->position(),e->angleDelta().y(),e->buttons(),e->modifiers());e->accept();}
void SpineScene::hoverMoveEvent(QHoverEvent* e){if(m_controller)m_controller->hover(e->position());}
void SpineScene::hoverLeaveEvent(QHoverEvent*){if(m_controller)m_controller->hover(QPointF(-1,-1));}
}
