import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Spotter

Item {
    RowLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 16

        // The camera and the skeleton.
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "black"
            radius: 8
            clip: true
            CameraView {
                id: view
                anchors.fill: parent
            }
            SkeletonOverlay {
                anchors.fill: parent
                area: view.paintedRect
            }
            Label {
                anchors.centerIn: parent
                visible: !Coach.running
                text: Coach.status
                color: Theme.muted
                font.pixelSize: 18
            }
            // The cue, over the picture where the lifter can see it.
            Rectangle {
                visible: Coach.cue.length > 0
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottomMargin: 20
                width: cueText.implicitWidth + 32
                height: cueText.implicitHeight + 20
                radius: 8
                color: "#cc000000"
                border.color: Theme.warn
                Label {
                    id: cueText
                    anchors.centerIn: parent
                    text: Coach.cue
                    color: Theme.warn
                    font.pixelSize: 17
                }
            }
        }

        // The set: what, how many, how deep into the current repetition.
        ColumnLayout {
            Layout.preferredWidth: 380
            Layout.maximumWidth: 380
            Layout.fillWidth: false
            Layout.fillHeight: true
            spacing: 12

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 230
                radius: 8
                color: Theme.card
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 4
                    Label {
                        text: Coach.exercise ? History.exerciseName(Coach.exercise) : "Waiting for a set"
                        color: Coach.exercise ? Theme.text : Theme.muted
                        font.pixelSize: 20
                        font.bold: true
                    }
                    Label {
                        text: Coach.planned ? "Planned: " + Coach.planned : "Recognising the exercise by itself"
                        color: Theme.muted
                        font.pixelSize: 13
                    }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: Coach.reps
                        color: Theme.accent
                        font.pixelSize: 96
                        font.bold: true
                    }
                    Rectangle {  // how far into the repetition
                        Layout.fillWidth: true
                        height: 8
                        radius: 4
                        color: Theme.line
                        Rectangle {
                            width: parent.width * Coach.progress
                            height: parent.height
                            radius: 4
                            color: Theme.accent
                            Behavior on width { NumberAnimation { duration: 60 } }
                        }
                    }
                }
            }

            // A set from the plan, or let the classifier decide.
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: planCol.implicitHeight + 24
                radius: 8
                color: Theme.card
                ColumnLayout {
                    id: planCol
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8
                    RowLayout {
                        ComboBox {
                            id: pick
                            Layout.fillWidth: true
                            model: Coach.exercises
                            displayText: History.exerciseName(currentText)
                            delegate: ItemDelegate {
                                required property string modelData
                                width: pick.width
                                text: History.exerciseName(modelData)
                            }
                        }
                        SpinBox { id: reps; from: 1; to: 50; value: 10; implicitWidth: 150; editable: true }
                    }
                    RowLayout {
                        SpinBox { id: kg; from: 0; to: 400; value: 0; implicitWidth: 150; editable: true }
                        Label { text: "kg"; color: Theme.muted }
                        Item { Layout.fillWidth: true }
                        Button { text: "Count this"; onClicked: Coach.planSet(pick.currentText, reps.value, kg.value) }
                        Button { text: "Auto"; enabled: Coach.planned.length > 0; onClicked: Coach.clearPlan() }
                    }
                }
            }

            // The sets so far, and how hard the last one felt.
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 8
                color: Theme.card
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    Label { text: "This workout"; color: Theme.muted; font.pixelSize: 13 }
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: Coach.sets
                        delegate: Label {
                            required property var modelData
                            width: ListView.view.width
                            text: modelData.name + " " + modelData.set + ": " + modelData.reps + (modelData.planned !== modelData.reps ? "/" + modelData.planned : "")
                                  + (modelData.weight > 0 ? " @ " + modelData.weight + " kg" : "")
                                  + (modelData.faults > 0 ? "  ·  " + modelData.faults + " with faults" : "")
                                  + (modelData.rpe ? "  ·  RPE " + modelData.rpe : "")
                            color: Theme.text
                            font.pixelSize: 14
                            padding: 2
                        }
                    }
                    RowLayout {
                        visible: Coach.sets.length > 0
                        Label { text: "Last set felt (RPE)"; color: Theme.muted; font.pixelSize: 13 }
                        Repeater {
                            model: [6, 7, 8, 9, 10]
                            delegate: Button {
                                required property int modelData
                                text: modelData
                                implicitWidth: 38
                                onClicked: Coach.rateLastSet(modelData)
                            }
                        }
                    }
                }
            }

            RowLayout {
                Button { text: "Camera"; onClicked: Coach.startCamera(0) }
                Button { text: "Video…"; onClicked: dialog.open() }
                Button { text: "Stop"; enabled: Coach.running; onClicked: Coach.stop() }
                Item { Layout.fillWidth: true }
                Switch { text: "Voice"; checked: Coach.voice; onToggled: Coach.voice = checked }
            }
        }
    }

    FileDialog {
        id: dialog
        nameFilters: ["Videos (*.mp4 *.avi *.mov *.mkv *.webm)"]
        onAccepted: Coach.openVideo(selectedFile, true)
    }
}
