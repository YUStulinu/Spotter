import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Spotter

Item {
    id: page
    signal startSet(string exercise, int reps, real weight)

    RowLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 16

        // Who is training and how they feel today.
        Rectangle {
            Layout.preferredWidth: 320
            Layout.fillHeight: true
            radius: 8
            color: Theme.card
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 10
                Label { text: "You"; color: Theme.text; font.pixelSize: 18; font.bold: true }
                Label { text: "Goal"; color: Theme.muted }
                ComboBox { id: goal; Layout.fillWidth: true; model: ["strength", "muscle", "general"] }
                Label { text: "Experience"; color: Theme.muted }
                ComboBox { id: experience; Layout.fillWidth: true; model: ["beginner", "intermediate"] }
                RowLayout {
                    Label { text: "Days a week"; color: Theme.muted; Layout.fillWidth: true }
                    SpinBox { id: days; from: 2; to: 4; value: 3 }
                }
                RowLayout {
                    Label { text: "Body weight (kg)"; color: Theme.muted; Layout.fillWidth: true }
                    SpinBox { id: bodyweight; from: 35; to: 200; value: 75; editable: true }
                }
                CheckBox { id: barbell; text: "I have a barbell"; checked: true }
                Label { text: "Fatigue today: " + fatigue.value + "/10"; color: Theme.muted }
                Slider { id: fatigue; Layout.fillWidth: true; from: 1; to: 10; stepSize: 1; value: 4 }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: "The plan reads your workout log: sets the camera counted, form faults, the effort you rated."
                    color: Theme.muted
                    font.pixelSize: 12
                }
                Item { Layout.fillHeight: true }
                Button {
                    Layout.fillWidth: true
                    text: History.planning ? "Planning…" : "Make my plan"
                    enabled: !History.planning
                    onClicked: History.makePlan(goal.currentText, days.value, experience.currentText, bodyweight.value, barbell.checked, fatigue.value)
                }
            }
        }

        // The plan.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8
            Label {
                text: History.plan.summary || "No plan yet"
                color: Theme.text
                font.pixelSize: 18
                font.bold: true
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
            Label { text: History.planStatus; color: Theme.muted; font.pixelSize: 13 }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                ColumnLayout {
                    width: parent.width
                    spacing: 12
                    Repeater {
                        model: History.plan.days || []
                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: dayCol.implicitHeight + 24
                            radius: 8
                            color: Theme.card
                            ColumnLayout {
                                id: dayCol
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 6
                                Label { text: modelData.name; color: Theme.accent; font.pixelSize: 16; font.bold: true }
                                Repeater {
                                    model: modelData.exercises
                                    delegate: RowLayout {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        spacing: 12
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 0
                                            Label {
                                                text: History.exerciseName(modelData.exercise) + "  " + modelData.sets + " × " + modelData.reps
                                                      + (modelData.weight_kg > 0 ? " @ " + modelData.weight_kg + " kg" : "")
                                                color: Theme.text
                                                font.pixelSize: 15
                                            }
                                            Label {
                                                Layout.fillWidth: true
                                                text: modelData.why
                                                color: Theme.muted
                                                font.pixelSize: 12
                                                wrapMode: Text.Wrap
                                            }
                                            Label {
                                                Layout.fillWidth: true
                                                visible: (modelData.adjusted || "").length > 0
                                                text: "Adjusted: " + (modelData.adjusted || "")
                                                color: Theme.warn
                                                font.pixelSize: 12
                                                wrapMode: Text.Wrap
                                            }
                                        }
                                        Button {
                                            text: "Start"
                                            onClicked: page.startSet(modelData.exercise, modelData.reps, modelData.weight_kg)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
