import QtQuick 2.2
import QtQuick.Controls 1.1

Rectangle {
    id: item 
    width: 1920
    height: 1080
    property bool isRedTop: false
    property int currentFrameNumber: 0

    signal qmlSignal()

    MouseArea {
        id: mousearea1
        x: 0
        y: 0
        anchors.rightMargin: 0
        anchors.bottomMargin: 0
        anchors.leftMargin: 0
        anchors.topMargin: 0
        anchors.fill: parent
        onClicked: {
            item.qmlSignal()
            console.log("mouse clicked !")
        }
    }

    Column {
        anchors.centerIn: parent
        Image { source: "image://VncImageProvider/" + currentFrameNumber }
    }

    Connections {
        target: VncImageProvider
        onSignalNewFrameReady: {
            console.log("onSignalNewFrameReady", frameNumber);
            currentFrameNumber = frameNumber;
        }
    }
}
