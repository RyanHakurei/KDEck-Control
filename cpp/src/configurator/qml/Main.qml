import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard

Kirigami.ApplicationWindow {
    id: window

    title: i18n("Galleon Deck")
    width: 1100
    height: 760
    minimumWidth: 860
    minimumHeight: 620

    pageStack.initialPage: keysPage

    Kirigami.Page {
        id: keysPage

        title: i18n("Keys")

        actions: [
            Kirigami.Action {
                text: i18n("Save")
                icon.name: "document-save"
                enabled: controller.dirty
                onTriggered: controller.save()
            },
            Kirigami.Action {
                text: i18n("Reload")
                icon.name: "view-refresh"
                onTriggered: controller.reload()
            }
        ]

        RowLayout {
            anchors.fill: parent
            spacing: Kirigami.Units.largeSpacing

            ColumnLayout {
                Layout.preferredWidth: 460
                Layout.fillHeight: true
                spacing: Kirigami.Units.smallSpacing

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    visible: controller.error.length > 0
                    type: Kirigami.MessageType.Error
                    text: controller.error
                }

                RowLayout {
                    Layout.fillWidth: true
                    QQC2.ComboBox {
                        Layout.fillWidth: true
                        model: controller.profiles
                        currentIndex: Math.max(0, controller.profiles.indexOf(controller.profile))
                        onActivated: controller.profile = currentText
                    }
                    QQC2.ComboBox {
                        Layout.fillWidth: true
                        model: controller.pages
                        currentIndex: controller.page
                        onActivated: controller.page = currentIndex
                    }
                }

                Kirigami.Heading {
                    level: 3
                    text: controller.profile + (controller.pageName.length ? "  /  " + controller.pageName : "")
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                GridLayout {
                    columns: 3
                    columnSpacing: Kirigami.Units.smallSpacing
                    rowSpacing: Kirigami.Units.smallSpacing
                    Layout.alignment: Qt.AlignHCenter

                    Repeater {
                        model: 12

                        QQC2.Button {
                            id: keyButton

                            Layout.preferredWidth: 140
                            Layout.preferredHeight: 140
                            padding: 6
                            onClicked: controller.keyIndex = index

                            background: Rectangle {
                                radius: 8
                                color: keyButton.down ? Kirigami.Theme.alternateBackgroundColor : Kirigami.Theme.backgroundColor
                                border.width: controller.keyIndex === index ? 2 : 1
                                border.color: controller.keyIndex === index ? Kirigami.Theme.highlightColor : Kirigami.Theme.separatorColor
                            }

                            contentItem: Image {
                                source: "image://keys/" + index + "/" + controller.revision
                                fillMode: Image.PreserveAspectFit
                                cache: false
                                smooth: true
                            }
                        }
                    }
                }

                Item {
                    Layout.fillHeight: true
                }

                QQC2.Label {
                    text: controller.status
                    color: Kirigami.Theme.disabledTextColor
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }

            QQC2.ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true

                ColumnLayout {
                    width: Math.max(implicitWidth, parent.width)
                    spacing: Kirigami.Units.largeSpacing

                    FormCard.FormHeader {
                        title: i18n("Key %1", controller.keyIndex + 1)
                    }

                    Kirigami.InlineMessage {
                        Layout.fillWidth: true
                        visible: controller.special.length > 0
                        text: controller.special
                    }

                    FormCard.FormCard {
                        Layout.fillWidth: true

                        FormCard.FormTextFieldDelegate {
                            label: i18n("Label")
                            text: controller.label
                            onEditingFinished: controller.label = text
                        }
                        FormCard.FormTextFieldDelegate {
                            label: i18n("Icon")
                            description: i18n("A glyph from Symbols Nerd Font")
                            text: controller.icon
                            onEditingFinished: controller.icon = text
                        }
                        FormCard.FormTextDelegate {
                            text: controller.image.length ? controller.image : i18n("No image")
                            description: i18n("PNG, JPEG, WebP, or GIF")
                        }
                        FormCard.FormButtonDelegate {
                            text: i18n("Choose image")
                            icon.name: "insert-image"
                            onClicked: imageDialog.open()
                        }
                        FormCard.FormButtonDelegate {
                            text: i18n("Clear image")
                            icon.name: "edit-clear"
                            enabled: controller.image.length > 0
                            onClicked: controller.clearImage()
                        }
                    }

                    FormCard.FormHeader {
                        title: i18n("Action")
                        visible: controller.special.length === 0
                    }

                    FormCard.FormCard {
                        Layout.fillWidth: true
                        visible: controller.special.length === 0

                        FormCard.FormComboBoxDelegate {
                            text: i18n("Kind")
                            model: controller.actionKindLabels
                            currentIndex: controller.actionKindIndex
                            onActivated: index => controller.actionKindIndex = index
                        }
                        FormCard.FormTextFieldDelegate {
                            label: i18n("Value")
                            description: {
                                const kinds = ["", i18n("A key, such as KP7 or F5"), i18n("A shortcut, such as SUPER+SHIFT+S"),
                                    i18n("A command to run"), i18n("play-pause, next, previous, or stop"),
                                    i18n("A page name, next, previous, or back"), i18n("A profile name"),
                                    i18n("Keys to tap, such as W S D"), i18n("A key to keep tapping")]
                                return kinds[controller.actionKindIndex] || ""
                            }
                            visible: controller.actionKindIndex > 0
                            text: controller.actionValue
                            onEditingFinished: controller.actionValue = text
                        }
                        FormCard.FormCheckDelegate {
                            text: i18n("Show success or failure")
                            visible: controller.actionKindIndex === 3
                            checked: controller.confirm
                            onToggled: controller.confirm = checked
                        }
                    }
                }
            }
        }
    }

    FileDialog {
        id: imageDialog
        title: i18n("Key image")
        nameFilters: [i18n("Images (*.png *.jpg *.jpeg *.webp *.gif *.apng)")]
        onAccepted: controller.setImageUrl(selectedFile)
    }
}
