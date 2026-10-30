import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Spotter

Item {
    RowLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 16

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            RowLayout {
                Label { text: "Progress"; color: Theme.text; font.pixelSize: 18; font.bold: true }
                Item { Layout.fillWidth: true }
                ComboBox {
                    id: which
                    implicitWidth: 220
                    model: History.exercises
                    displayText: History.exerciseName(currentText)
                    // Start on the squat when there is one: the lift most people follow.
                    Component.onCompleted: currentIndex = Math.max(0, History.exercises.indexOf("squats"))
                    delegate: ItemDelegate {
                        required property string modelData
                        width: which.width
                        text: History.exerciseName(modelData)
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 8
                color: Theme.card
                LineChart {
                    id: chart
                    anchors.fill: parent
                    anchors.margins: 16
                    property var heavy: History.maxima(which.currentText)
                    // Body-weight exercises have no weight: chart the repetitions instead.
                    points: heavy.length > 0 ? heavy : History.volume(which.currentText)
                    unit: heavy.length > 0 ? "estimated 1RM, kg" : "repetitions per session"
                    Connections {
                        target: History
                        function onChanged() { chart.heavy = History.maxima(which.currentText) }
                    }
                }
                Label {
                    anchors.centerIn: parent
                    visible: History.exercises.length === 0
                    text: "Train with the camera: your sets appear here"
                    color: Theme.muted
                }
            }
        }

        Rectangle {
            Layout.preferredWidth: 380
            Layout.fillHeight: true
            radius: 8
            color: Theme.card
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                Label { text: "Workouts"; color: Theme.muted; font.pixelSize: 13 }
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 10
                    model: History.workouts
                    delegate: ColumnLayout {
                        required property var modelData
                        width: ListView.view.width
                        spacing: 2
                        Label { text: modelData.date + (modelData.fatigue ? "  ·  fatigue " + modelData.fatigue + "/10" : ""); color: Theme.text; font.bold: true }
                        Label { Layout.fillWidth: true; text: modelData.sets; color: Theme.muted; wrapMode: Text.Wrap; font.pixelSize: 13 }
                    }
                }
            }
        }
    }
}
