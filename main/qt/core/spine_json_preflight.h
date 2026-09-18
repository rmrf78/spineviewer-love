#pragma once
#include "../../../third_party/nlohmann/json.hpp"
#include <QByteArray>

namespace slqt {

class SpineJsonPreflight final : public nlohmann::json_sax<nlohmann::json> {
    int depth=0;
    bool objectRoot=false,bonesArray=false,hasBones=false;
    std::string rootKey;
    void value(){
        if(depth==1&&rootKey=="bones")hasBones=false;
        if(depth==2&&bonesArray)hasBones=true;
    }
public:
    bool null()override{value();return true;}
    bool boolean(bool)override{value();return true;}
    bool number_integer(number_integer_t)override{value();return true;}
    bool number_unsigned(number_unsigned_t)override{value();return true;}
    bool number_float(number_float_t,const string_t&)override{value();return true;}
    bool string(string_t&)override{value();return true;}
    bool binary(binary_t&)override{value();return true;}
    bool start_object(std::size_t)override{if(depth>=1024)return false;value();if(depth==0)objectRoot=true;++depth;return true;}
    bool key(string_t& key)override{if(depth==1)rootKey=key;return true;}
    bool end_object()override{--depth;return true;}
    bool start_array(std::size_t)override{
        if(depth>=1024)return false;value();if(depth==0)return false;
        if(depth==1)bonesArray=rootKey=="bones";
        ++depth;return true;
    }
    bool end_array()override{if(depth==2)bonesArray=false;--depth;return true;}
    bool parse_error(std::size_t,const std::string&,const nlohmann::detail::exception&)override{return false;}
    static bool valid(const QByteArray& bytes){
        SpineJsonPreflight check;
        return nlohmann::json::sax_parse(bytes.constData(),bytes.constData()+bytes.size(),&check)
            &&check.objectRoot&&check.hasBones;
    }
};
}
