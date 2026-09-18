pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

Item {
    id: pet
    required property var shell
    readonly property real petScale: Math.max(.75,shell.read("titleScale",1))*shell.metrics.pixel
    anchors.fill: parent
    visible: shell.read("petMode", false)
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        onClicked: function(mouse) { menu.x=mouse.x;menu.y=mouse.y;menu.open(); }
    }
    Popup {
        id: menu
        objectName: "petContextMenu"
        width: 200*pet.petScale
        padding: 10*pet.petScale
        background: Rectangle { color: pet.shell.theme.popup; radius: 10*pet.petScale }
        contentItem: Column {
            spacing: pet.shell.metrics.spacing
            SlCheckBox { width: parent.width; metrics: pet.shell.metrics; theme: pet.shell.theme; lineHeight: metrics.detailFont; text: qsTr("Random Motion"); checked: pet.shell.read("petRandom",true); enabled: pet.shell.can("pet.random"); onClicked: pet.shell.send("pet.random",checked) }
            SlButton { width: 180*pet.petScale; height: 40*pet.petScale; metrics: pet.shell.metrics; theme: pet.shell.theme; lineHeight: metrics.detailFont; text: qsTr("Next Motion"); enabled: pet.shell.can("pet.next"); onClicked: {pet.shell.send("pet.next",null);menu.close();} }
            SlButton { objectName: "petExit"; width: 180*pet.petScale; height: 40*pet.petScale; metrics: pet.shell.metrics; theme: pet.shell.theme; lineHeight: metrics.detailFont; text: qsTr("Exit Desktop Pet"); enabled: pet.shell.can("pet.exit"); onClicked: {menu.close();pet.shell.send("pet.exit",null);} }
        }
    }
}
