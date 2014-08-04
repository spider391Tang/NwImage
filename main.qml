import QtQuick 2.2
import QtQuick.Controls 1.1

Rectangle {
    width:1920
    height: 1080
    property bool isRedTop: false
    property int currentFrameNumber: 0

    Column {
        anchors.centerIn: parent
        Image { source: "image://NwImageProvider/" + currentFrameNumber }
    }

    Connections {
        target: NwImageProvider
        onSignalNewFrameReady: {
            console.log("onSignalNewFrameReady", frameNumber);
            currentFrameNumber = frameNumber;
        }
    }
}
