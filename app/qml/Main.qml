import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1280
    height: 800
    visible: true
    title: "Spotter"
    color: Theme.bg

    function show(name) { tabs.currentIndex = ["train", "plan", "progress"].indexOf(name) }
    Component.onCompleted: show(startPage)

    header: Rectangle {
        height: 56
        color: Theme.panel
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            spacing: 24
            Label {
                text: "Spotter"
                color: Theme.accent
                font.pixelSize: 22
                font.bold: true
            }
            Repeater {
                model: ["Train", "Plan", "Progress"]
                delegate: Label {
                    required property string modelData
                    required property int index
                    text: modelData
                    color: tabs.currentIndex === index ? Theme.text : Theme.muted
                    font.pixelSize: 16
                    font.bold: tabs.currentIndex === index
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: tabs.currentIndex = index
                    }
                }
            }
            Item { Layout.fillWidth: true }
            Label {
                text: Coach.device ? Coach.device + (Coach.running ? " · " + Coach.fps.toFixed(0) + " fps" : "") : ""
                color: Theme.muted
                font.pixelSize: 13
            }
        }
    }

    StackLayout {
        id: tabs
        anchors.fill: parent
        TrainView {}
        PlanView { onStartSet: (exercise, reps, weight) => { Coach.planSet(exercise, reps, weight); tabs.currentIndex = 0 } }
        ProgressView {}
    }
}
