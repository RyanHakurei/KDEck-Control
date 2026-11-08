import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import QtQuick.Layouts
import Qt.labs.platform as Labs
import org.kde.kirigami as Kirigami

Kirigami.ApplicationWindow {
    id: window

    title: controller.profile.length > 0 ? i18n("%1 — Galleon Deck", controller.profile) : i18n("Galleon Deck")
    width: 1180
    height: 820
    minimumWidth: 1100
    minimumHeight: 680

    pageStack.initialPage: editorPage

    function syncText(field, value) {
        if (field.activeFocus || field.text === value)
            return
        field.text = value
    }

    // AT-SPI SetTextContents clears the field and then inserts the new text.
    // Committing the empty value clamps a speed to its minimum and overwrites
    // the field before the insert lands. Read the field on the next turn.
    function commitNumber(field, current, apply) {
        const raw = field.text.trim()
        if (raw.length === 0)
            return
        const value = Number(raw)
        if (isNaN(value))
            return
        if (Math.abs(value - current) > 0.001)
            apply(value)
    }

    function commitString(field, current, apply) {
        if (field.text !== current)
            apply(field.text)
    }

    function syncCombo(box, index) {
        if (box.popup.opened || box.currentIndex === index)
            return
        box.currentIndex = index
    }

    function addMediaPlayer() {
        const name = playerField.text.trim()
        if (name.length === 0)
            return
        if (controller.setMediaPlayerEnabled(name, true))
            playerField.clear()
    }

    function stepCombo(box, direction) {
        const next = box.currentIndex + direction
        if (next < 0 || next >= box.count)
            return
        box.currentIndex = next
        box.activated(next)
    }

    function syncEditor() {
        syncText(labelField, controller.label)
        syncText(iconField, controller.icon)
        syncCombo(fgBox, colourIndex(controller.fg))
        syncText(fgCustom, controller.fg)
        syncCombo(bgBox, colourIndex(controller.bg))
        syncText(bgCustom, controller.bg)
        syncCombo(borderBox, colourIndex(controller.border))
        syncText(borderCustom, controller.border)
        syncCombo(kindBox, controller.actionKindIndex)
        syncCombo(pressBox, controller.pressKindIndex)
        syncCombo(statBox, controller.statIndex)
        syncCombo(mediaBox, Math.max(0, controller.mediaChoices.indexOf(controller.actionValue)))
        syncCombo(pageTargetBox, Math.max(0, controller.pageChoices.indexOf(controller.actionValue)))
        syncCombo(profileTargetBox, Math.max(0, controller.profiles.indexOf(controller.actionValue)))
        syncText(valueField, controller.actionValue)
        syncText(appArgsField, controller.appArgs)
        syncText(holdField, controller.hold)
        if (!repeatSpin.activeFocus && repeatSpin.value !== controller.repeatMs)
            repeatSpin.value = controller.repeatMs
        if (!stepSpin.activeFocus && stepSpin.value !== controller.stepMs)
            stepSpin.value = controller.stepMs
        if (!confirmBox.activeFocus && confirmBox.checked !== controller.confirm)
            confirmBox.checked = controller.confirm
        syncCombo(startPageBox, Math.max(0, controller.pages.indexOf(controller.startPage)))
        if (!settingsBox.activeFocus && settingsBox.checked !== controller.settingsPage)
            settingsBox.checked = controller.settingsPage
        syncText(logoField, controller.logo)
        if (!wallpaperAutoBox.activeFocus && wallpaperAutoBox.checked !== controller.wallpaperAuto)
            wallpaperAutoBox.checked = controller.wallpaperAuto
        syncText(wallpaperCommandField, controller.wallpaperCommand)
        if (!transitionEnabledBox.activeFocus && transitionEnabledBox.checked !== controller.transitionEnabled)
            transitionEnabledBox.checked = controller.transitionEnabled
        syncText(transitionSpeedField, controller.transitionSpeed.toFixed(2))
        syncText(transitionHoldField, controller.transitionHold.toFixed(1))
        if (!bootEnabledBox.activeFocus && bootEnabledBox.checked !== controller.bootEnabled)
            bootEnabledBox.checked = controller.bootEnabled
        syncText(bootSpeedField, controller.bootSpeed.toFixed(2))
        syncText(bootTitleField, controller.bootTitle)
        syncText(bootLinesField, controller.bootLinesText)
        syncText(bootLogoField, controller.bootLogo)
        syncText(bootGifField, controller.bootGif)
        syncCombo(fallbackBox, controller.autoFallback.length === 0 ? 0 : Math.max(0, controller.profiles.indexOf(controller.autoFallback) + 1))
        if (!brightnessSlider.pressed && brightnessSlider.value !== controller.brightness)
            brightnessSlider.value = controller.brightness
        if (!volumeSpin.activeFocus && volumeSpin.value !== controller.volumeStep)
            volumeSpin.value = controller.volumeStep
        syncText(clockField, controller.clockFormat)
        syncCombo(startProfileBox, Math.max(0, controller.profiles.indexOf(controller.startProfile)))
    }

    Component.onCompleted: syncEditor()

    Connections {
        target: controller
        function onChanged() {
            syncEditor()
        }
        function onRecordFinished() {
            recordDialog.close()
        }
    }

    Kirigami.Page {
        id: editorPage

        title: controller.profile
        padding: Kirigami.Units.largeSpacing

        // A plain item, not a layout: the header loader stretches a layout
        // across the title slot and shoves the edit button away from the combo.
        titleDelegate: Item {
            id: titleRow

            readonly property int clusterWidth: headerStatus.text.length > 0
                ? headerStatus.x + headerStatus.implicitWidth
                : profileActions.x + profileActions.width

            implicitWidth: clusterWidth
            implicitHeight: Math.max(profileBox.implicitHeight, profileActions.implicitHeight)
            Layout.preferredWidth: implicitWidth
            Layout.maximumWidth: 680
            width: implicitWidth
            height: implicitHeight

            QQC2.ComboBox {
                id: profileBox
                width: 260
                anchors.verticalCenter: parent.verticalCenter
                model: controller.profiles
                Accessible.name: i18n("Profile")
                onActivated: controller.profile = currentText
                Accessible.onIncreaseAction: window.stepCombo(profileBox, 1)
                Accessible.onDecreaseAction: window.stepCombo(profileBox, -1)

                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.text: controller.status

                function followProfile() {
                    if (popup.opened)
                        return
                    const index = Math.max(0, controller.profiles.indexOf(controller.profile))
                    if (currentIndex !== index)
                        currentIndex = index
                }

                Component.onCompleted: followProfile()

                Connections {
                    target: controller
                    function onChanged() {
                        profileBox.followProfile()
                    }
                }
            }
            QQC2.ToolButton {
                id: profileActions
                x: profileBox.width + Kirigami.Units.smallSpacing
                anchors.verticalCenter: parent.verticalCenter
                icon.name: "document-edit"
                display: QQC2.AbstractButton.IconOnly
                Accessible.name: i18n("Profile actions")
                onClicked: profileMenu.popup(profileActions, 0, height)
                Accessible.onPressAction: profileMenu.popup(profileActions, 0, height)

                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.text: i18n("Profile actions")
            }
            Kirigami.Separator {
                x: profileActions.x + profileActions.width + Kirigami.Units.smallSpacing
                anchors.verticalCenter: parent.verticalCenter
                height: parent.height
                visible: headerStatus.text.length > 0
            }
            QQC2.Label {
                id: headerStatus
                x: profileActions.x + profileActions.width + Kirigami.Units.largeSpacing
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(280, implicitWidth)
                elide: Text.ElideRight
                color: Kirigami.Theme.disabledTextColor
                text: {
                    const status = controller.status
                    if (status.length === 0 || status === i18n("Unsaved changes"))
                        return ""
                    // The standing line only names the config directory. Real results still show.
                    if (status.indexOf("/galleon-deck") !== -1 && status.indexOf(".toml") === -1)
                        return ""
                    return status
                }
                visible: text.length > 0
                Accessible.name: text
            }
        }

        actions: [
            Kirigami.Action {
                text: controller.serviceRunning ? i18n("Deck running") : i18n("Service stopped")
                icon.name: controller.serviceRunning ? "media-playback-playing" : "process-stop"
                icon.color: controller.serviceRunning ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.neutralTextColor
                tooltip: controller.serviceRunning ? i18n("The deck service is running") : i18n("The deck service is stopped")
                displayHint: Kirigami.DisplayHint.KeepVisible
                onTriggered: {
                    if (serviceMenu.opened)
                        serviceMenu.close()
                    else
                        serviceMenu.popup(window.contentItem, Math.max(0, window.width - 320), Kirigami.Units.gridUnit * 2)
                }
            },
            Kirigami.Action {
                text: i18n("Reload")
                icon.name: "view-refresh"
                tooltip: i18n("Reload from disk")
                shortcut: StandardKey.Refresh
                displayHint: Kirigami.DisplayHint.KeepVisible
                onTriggered: controller.reload()
            },
            Kirigami.Action {
                text: i18n("Open config folder")
                icon.name: "folder-open"
                tooltip: i18n("Open config folder")
                displayHint: Kirigami.DisplayHint.KeepVisible | Kirigami.DisplayHint.IconOnly
                onTriggered: controller.openConfigFolder()
            },
            Kirigami.Action {
                text: i18n("About Galleon Deck")
                icon.name: "help-about"
                tooltip: i18n("About Galleon Deck")
                displayHint: Kirigami.DisplayHint.KeepVisible | Kirigami.DisplayHint.IconOnly
                onTriggered: aboutDialog.open()
            }
        ]

        RowLayout {
            anchors.fill: parent
            spacing: Kirigami.Units.smallSpacing

            Flickable {
                id: deckScroll

                Layout.preferredWidth: 600
                Layout.minimumWidth: 576
                Layout.maximumWidth: 620
                Layout.fillHeight: true
                contentWidth: width
                contentHeight: deckColumn.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                ColumnLayout {
                    id: deckColumn
                    width: deckScroll.width
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.InlineMessage {
                        Layout.fillWidth: true
                        visible: controller.error.length > 0
                        type: Kirigami.MessageType.Error
                        text: controller.error
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignHCenter
                        Layout.maximumWidth: 576
                        spacing: Kirigami.Units.smallSpacing

                        Flickable {
                            id: pageFlick
                            Layout.fillWidth: true
                            Layout.preferredHeight: pageRow.implicitHeight
                            contentWidth: pageRow.implicitWidth
                            contentHeight: pageRow.implicitHeight
                            flickableDirection: Flickable.HorizontalFlick
                            clip: true
                            // A short row must not steal the pointer, or a page drag never starts.
                            interactive: contentWidth > width

                            Row {
                                id: pageRow
                                spacing: Kirigami.Units.smallSpacing
                                property bool draggingPage: false
                                property bool suppressPageClick: false

                                Repeater {
                                    model: controller.pages

                                    QQC2.Button {
                                        id: pageButton
                                        required property int index
                                        required property string modelData

                                        text: modelData
                                        highlighted: controller.page === index
                                        flat: controller.page !== index
                                        icon.name: controller.pageIsSettings(index) ? "emblem-system-symbolic" : ""
                                        display: icon.name.length > 0 ? QQC2.AbstractButton.TextBesideIcon : QQC2.AbstractButton.TextOnly
                                        Accessible.name: modelData
                                        Accessible.onPressAction: controller.page = index
                                        onClicked: {
                                            if (pageRow.suppressPageClick)
                                                return
                                            controller.page = index
                                        }

                                        QQC2.ToolTip.visible: hovered && controller.pageIsSettings(index)
                                        QQC2.ToolTip.text: i18n("Settings page (generated: themes, brightness, profiles)")

                                        Drag.active: pageDrag.active && pageDrag.moved
                                        Drag.dragType: Drag.Internal
                                        Drag.supportedActions: Qt.MoveAction
                                        Drag.hotSpot.x: width / 2
                                        Drag.hotSpot.y: height / 2
                                        Drag.mimeData: {"text/galleon-page": String(index)}

                                        DragHandler {
                                            id: pageDrag
                                            target: null
                                            enabled: !controller.pageIsSettings(pageButton.index)
                                            grabPermissions: PointerHandler.CanTakeOverFromAnything
                                            property bool moved: false
                                            onActiveChanged: {
                                                pageRow.draggingPage = active
                                                if (active) {
                                                    moved = false
                                                } else if (moved) {
                                                    pageRow.suppressPageClick = true
                                                    Qt.callLater(function() { pageRow.suppressPageClick = false })
                                                }
                                            }
                                            onTranslationChanged: {
                                                if (Math.abs(translation.x) > 8 || Math.abs(translation.y) > 8)
                                                    moved = true
                                            }
                                        }
                                        DropArea {
                                            id: pageDrop
                                            anchors.fill: parent
                                            enabled: !controller.pageIsSettings(pageButton.index)
                                            onDropped: function(drop) {
                                                const text = drop.getDataAsString("text/galleon-page")
                                                const from = parseInt(text)
                                                if (text.length > 0 && !isNaN(from) && from !== pageButton.index)
                                                    controller.movePageTo(from, pageButton.index)
                                            }
                                        }
                                        Rectangle {
                                            anchors.fill: parent
                                            radius: 4
                                            color: "transparent"
                                            border.width: pageDrop.containsDrag ? 2 : 0
                                            border.color: Kirigami.Theme.highlightColor
                                        }
                                    }
                                }
                            }
                        }
                        QQC2.ToolButton {
                            id: pageActions
                            icon.name: "document-edit"
                            display: QQC2.AbstractButton.IconOnly
                            Accessible.name: i18n("Page actions")
                            onClicked: pageMenu.popup(pageActions, 0, height)
                            Accessible.onPressAction: pageMenu.popup(pageActions, 0, height)

                            QQC2.ToolTip.visible: hovered
                            QQC2.ToolTip.text: i18n("Page actions")
                        }
                        Kirigami.ContextualHelpButton {
                            toolTipText: i18n("Click a key to edit it. Double-click a folder to open it. Drag keys or pages to rearrange them. Edits save on their own.")
                        }
                    }

                    RowLayout {
                        visible: controller.folderDepth > 0
                        Layout.alignment: Qt.AlignHCenter
                        spacing: Kirigami.Units.smallSpacing

                        Repeater {
                            model: controller.folderDepth > 0 ? controller.folderCrumbs : []

                            RowLayout {
                                required property int index
                                required property string modelData
                                spacing: Kirigami.Units.smallSpacing

                                QQC2.Label {
                                    visible: index > 0
                                    text: "›"
                                    color: Kirigami.Theme.disabledTextColor
                                }
                                QQC2.ToolButton {
                                    text: modelData
                                    flat: index < controller.folderDepth
                                    highlighted: index === controller.folderDepth
                                    enabled: index < controller.folderDepth
                                    Accessible.name: i18n("Folder path %1", modelData)
                                    onClicked: if (index < controller.folderDepth) controller.showFolderDepth(index)
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: 576
                        implicitWidth: 576
                        implicitHeight: deckInner.implicitHeight + 36
                        radius: 18
                        color: "#000000"

                        ColumnLayout {
                            id: deckInner
                            x: 18
                            y: 18
                            width: 540
                            spacing: 14

                            Rectangle {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.preferredWidth: 540
                                Layout.preferredHeight: 288
                                radius: 6
                                color: "#000000"
                                clip: true
                                Accessible.name: i18n("Deck screen")

                                Image {
                                    anchors.fill: parent
                                    source: "image://keys/lcd/" + controller.previewRevision
                                    fillMode: Image.Stretch
                                    cache: false
                                    smooth: true
                                    asynchronous: false
                                }
                            }

                            Grid {
                                columns: 3
                                columnSpacing: 12
                                rowSpacing: 12
                                Layout.alignment: Qt.AlignHCenter

                                Repeater {
                                    model: 12

                                    QQC2.Button {
                                        id: keyButton

                                        required property int index

                                        width: 116
                                        height: 116
                                        padding: 0
                                        Accessible.name: i18n("Deck key %1", index + 1)
                                        onClicked: controller.keyIndex = index
                                        Accessible.onPressAction: controller.keyIndex = index
                                        onDoubleClicked: controller.openFolder(index)

                                        Drag.active: keyDrag.active
                                        Drag.dragType: Drag.Internal
                                        Drag.supportedActions: Qt.MoveAction
                                        Drag.hotSpot.x: width / 2
                                        Drag.hotSpot.y: height / 2
                                        Drag.mimeData: {"text/galleon-key": String(index)}

                                        DragHandler {
                                            id: keyDrag
                                            target: null
                                        }
                                        DropArea {
                                            anchors.fill: parent
                                            onDropped: function(drop) {
                                                const text = drop.getDataAsString("text/galleon-key")
                                                const from = parseInt(text)
                                                if (text.length > 0 && from >= 0)
                                                    controller.swapKeys(from, keyButton.index)
                                            }
                                        }

                                        background: Rectangle {
                                            anchors.fill: parent
                                            anchors.margins: controller.keyIndex === keyButton.index ? -5 : 0
                                            radius: controller.keyIndex === keyButton.index ? 13 : 10
                                            color: "transparent"
                                            border.width: controller.keyIndex === keyButton.index ? 3 : 0
                                            border.color: Kirigami.Theme.highlightColor
                                        }

                                        contentItem: Image {
                                            source: "image://keys/" + keyButton.index + "/" + controller.revision + "/" + controller.animRevision + "/" + controller.statRevision
                                            fillMode: Image.PreserveAspectFit
                                            cache: false
                                            smooth: true
                                        }
                                    }
                                }
                            }
                        }
                    }

                }
            }

            Kirigami.Separator {
                Layout.fillHeight: true
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                QQC2.TabBar {
                    id: sections

                    Layout.fillWidth: true
                    // The desktop style leaves this bar transparent, so the
                    // page scrolls through the tabs. Keep the buttons above
                    // the page and paint a solid bar behind them.
                    z: 2
                    background: Rectangle {
                        color: Kirigami.Theme.backgroundColor
                    }

                    QQC2.TabButton {
                        text: i18n("Key")
                        icon.name: "input-keyboard"
                        Accessible.onPressAction: sections.currentIndex = 0
                    }
                    QQC2.TabButton {
                        text: i18n("Look")
                        icon.name: "color-picker"
                        Accessible.onPressAction: sections.currentIndex = 1
                    }
                    QQC2.TabButton {
                        text: i18n("Settings")
                        icon.name: "configure"
                        Accessible.onPressAction: sections.currentIndex = 2
                    }
                    QQC2.TabButton {
                        text: i18n("Add-ons")
                        icon.name: "application-x-addon-symbolic"
                        Accessible.onPressAction: sections.currentIndex = 3
                    }
                }

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    currentIndex: Math.max(0, sections.currentIndex)
                    onCurrentIndexChanged: {
                        if (currentIndex === 1)
                            controller.refreshWallpaperHint()
                        if (currentIndex === 3)
                            controller.refreshAddons()
                    }

                    Flickable {
                        id: keyFlick

                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        contentWidth: width
                        contentHeight: keyColumn.implicitHeight
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds

                        ColumnLayout {
                            id: keyColumn
                            width: keyFlick.width
                            spacing: Kirigami.Units.smallSpacing
                            readonly property string activeKind: controller.actionKindId === "stat" ? controller.pressKindId : controller.actionKindId

                            RowLayout {
                                visible: controller.keyEditable
                                Layout.fillWidth: true
                                Layout.topMargin: Kirigami.Units.largeSpacing
                                Layout.leftMargin: Kirigami.Units.largeSpacing
                                Layout.rightMargin: Kirigami.Units.largeSpacing

                                Kirigami.Heading {
                                    level: 2
                                    text: i18n("Key %1", controller.keyIndex + 1)
                                    Layout.fillWidth: true
                                }
                                QQC2.Button {
                                    text: i18n("Clear")
                                    icon.name: "edit-clear"
                                    Accessible.name: i18n("Clear key")
                                    onClicked: controller.clearKey()
                                }
                            }

                            QQC2.Label {
                                visible: controller.keyEditable
                                Layout.fillWidth: true
                                Layout.leftMargin: Kirigami.Units.largeSpacing
                                Layout.rightMargin: Kirigami.Units.largeSpacing
                                wrapMode: Text.WordWrap
                                color: Kirigami.Theme.disabledTextColor
                                text: controller.folderDepth > 0
                                      ? i18n("Inside %1. Top-left is 1; keys run left to right, top to bottom.", controller.folderCrumbs.join(" / "))
                                      : i18n("Top-left is 1; keys run left to right, top to bottom.")
                            }

                            QQC2.Label {
                                visible: !controller.keyEditable
                                Layout.fillWidth: true
                                Layout.topMargin: Kirigami.Units.largeSpacing
                                Layout.leftMargin: Kirigami.Units.largeSpacing
                                Layout.rightMargin: Kirigami.Units.largeSpacing
                                wrapMode: Text.WordWrap
                                text: i18n("This page is generated: a key per theme, one that builds a theme from your wallpaper, brightness, and a key per profile. Turn it off under Look.")
                            }

                            QQC2.Button {
                                visible: controller.canOpenFolder
                                text: i18n("Open folder")
                                icon.name: "folder-open"
                                onClicked: controller.openFolder(controller.keyIndex)
                                Layout.leftMargin: Kirigami.Units.largeSpacing
                            }

                            Kirigami.FormLayout {
                                // A wide window used to put the labels beside
                                // the fields and stretch the row to the window
                                // edge. This pane stays stacked.
                                wideMode: false
                                Layout.fillWidth: true
                                Layout.maximumWidth: Math.min(keyFlick.width, Kirigami.Units.gridUnit * 32)
                                clip: true
                                visible: controller.keyEditable

                                QQC2.TextField {
                                    id: labelField
                                    Kirigami.FormData.label: i18n("Label")
                                    Layout.fillWidth: true
                                    Accessible.name: i18n("Label")
                                    onTextChanged: if (text !== controller.label) controller.label = text
                                }
                                RowLayout {
                                    Kirigami.FormData.label: i18n("Icon")
                                    Layout.fillWidth: true
                                    visible: controller.actionKindId !== "stat"

                                    QQC2.TextField {
                                        id: iconField
                                        Layout.fillWidth: true
                                        placeholderText: i18n("Symbols Nerd Font glyph")
                                        Accessible.name: i18n("Icon")
                                        font.family: "Symbols Nerd Font"
                                        font.pixelSize: 18
                                        onTextChanged: if (text !== controller.icon) controller.icon = text
                                    }
                                    QQC2.Button {
                                        text: i18n("Choose…")
                                        Accessible.name: i18n("Choose icon")
                                        onClicked: iconDialog.open()
                                    }
                                    QQC2.Button {
                                        text: i18n("No icon")
                                        Accessible.name: i18n("No icon")
                                        enabled: controller.icon.length > 0
                                        onClicked: controller.icon = ""
                                    }
                                }
                                QQC2.ComboBox {
                                    id: fgBox
                                    Kirigami.FormData.label: i18n("Text and icon colour")
                                    Layout.fillWidth: true
                                    model: colourModel
                                    onActivated: index => controller.fg = colourChoice(index, controller.fg)
                                }
                                QQC2.TextField {
                                    id: fgCustom
                                    Kirigami.FormData.label: i18n("Custom text colour")
                                    Layout.fillWidth: true
                                    visible: controller.fg.charAt(0) === "#"
                                    placeholderText: i18n("#rrggbb")
                                    onTextEdited: controller.fg = text
                                }
                                QQC2.ComboBox {
                                    id: bgBox
                                    Kirigami.FormData.label: i18n("Background")
                                    Layout.fillWidth: true
                                    model: colourModel
                                    onActivated: index => controller.bg = colourChoice(index, controller.bg)
                                }
                                QQC2.TextField {
                                    id: bgCustom
                                    Kirigami.FormData.label: i18n("Custom background")
                                    Layout.fillWidth: true
                                    visible: controller.bg.charAt(0) === "#"
                                    placeholderText: i18n("#rrggbb")
                                    onTextEdited: controller.bg = text
                                }
                                QQC2.ComboBox {
                                    id: borderBox
                                    Kirigami.FormData.label: i18n("Border")
                                    Layout.fillWidth: true
                                    model: colourModel
                                    onActivated: index => controller.border = colourChoice(index, controller.border)
                                }
                                QQC2.TextField {
                                    id: borderCustom
                                    Kirigami.FormData.label: i18n("Custom border")
                                    Layout.fillWidth: true
                                    visible: controller.border.charAt(0) === "#"
                                    placeholderText: i18n("#rrggbb")
                                    onTextEdited: controller.border = text
                                }
                                RowLayout {
                                    Kirigami.FormData.label: i18n("Image")
                                    Layout.fillWidth: true
                                    visible: controller.actionKindId !== "stat"

                                    QQC2.Label {
                                        Layout.fillWidth: true
                                        elide: Text.ElideMiddle
                                        text: controller.image.length ? controller.image : i18n("No image")
                                    }
                                    QQC2.Button {
                                        text: i18n("Choose")
                                        icon.name: "insert-image"
                                        onClicked: imageDialog.open()
                                    }
                                    QQC2.Button {
                                        text: i18n("Clear")
                                        icon.name: "edit-clear"
                                        Accessible.name: i18n("Clear image")
                                        enabled: controller.image.length > 0
                                        onClicked: controller.clearImage()
                                    }
                                }

                                Kirigami.Separator {
                                    Kirigami.FormData.isSection: true
                                    Kirigami.FormData.label: i18n("When pressed")
                                }
                                QQC2.ComboBox {
                                    id: kindBox
                                    Kirigami.FormData.label: i18n("Action")
                                    Layout.fillWidth: true
                                    model: controller.actionKindLabels
                                    Accessible.name: i18n("Action")
                                    onActivated: index => controller.actionKindIndex = index
                                    Accessible.onIncreaseAction: stepCombo(kindBox, 1)
                                    Accessible.onDecreaseAction: stepCombo(kindBox, -1)
                                    Accessible.onPressAction: popup.open()
                                }
                                QQC2.ComboBox {
                                    id: statBox
                                    Kirigami.FormData.label: i18n("Reading")
                                    Layout.fillWidth: true
                                    visible: controller.actionKindId === "stat"
                                    model: controller.statTitles
                                    Accessible.name: i18n("Reading")
                                    onActivated: index => controller.statIndex = index
                                    Accessible.onPressAction: popup.open()
                                    Accessible.onIncreaseAction: stepCombo(statBox, 1)
                                    Accessible.onDecreaseAction: stepCombo(statBox, -1)
                                }
                                QQC2.Label {
                                    Layout.fillWidth: true
                                    visible: controller.actionKindId === "stat"
                                    wrapMode: Text.WordWrap
                                    text: i18n("Live from KDE System Monitor. Uses this profile's theme.")
                                }
                                QQC2.ComboBox {
                                    id: pressBox
                                    Kirigami.FormData.label: i18n("Passthrough")
                                    Layout.fillWidth: true
                                    visible: controller.actionKindId === "stat"
                                    model: controller.pressKindLabels
                                    Accessible.name: i18n("Passthrough")
                                    onActivated: index => controller.pressKindIndex = index
                                    Accessible.onPressAction: popup.open()
                                    Accessible.onIncreaseAction: stepCombo(pressBox, 1)
                                    Accessible.onDecreaseAction: stepCombo(pressBox, -1)
                                }
                                QQC2.Label {
                                    Layout.fillWidth: true
                                    visible: controller.actionKindId === "stat"
                                    wrapMode: Text.WordWrap
                                    text: i18n("This still runs when the key is pressed.")
                                }
                                QQC2.ComboBox {
                                    id: mediaBox
                                    Kirigami.FormData.label: i18n("Control")
                                    Layout.fillWidth: true
                                    visible: keyColumn.activeKind === "media"
                                    model: controller.mediaChoices
                                    Accessible.name: i18n("Media control")
                                    onActivated: controller.actionValue = currentText
                                    Accessible.onPressAction: popup.open()
                                    Accessible.onIncreaseAction: stepCombo(mediaBox, 1)
                                    Accessible.onDecreaseAction: stepCombo(mediaBox, -1)
                                }
                                QQC2.ComboBox {
                                    id: pageTargetBox
                                    Kirigami.FormData.label: i18n("Page")
                                    Layout.fillWidth: true
                                    visible: keyColumn.activeKind === "page"
                                    model: controller.pageChoices
                                    Accessible.name: i18n("Page target")
                                    onActivated: controller.actionValue = currentText
                                    Accessible.onPressAction: popup.open()
                                    Accessible.onIncreaseAction: stepCombo(pageTargetBox, 1)
                                    Accessible.onDecreaseAction: stepCombo(pageTargetBox, -1)
                                }
                                QQC2.ComboBox {
                                    id: profileTargetBox
                                    Kirigami.FormData.label: i18n("Profile")
                                    Layout.fillWidth: true
                                    visible: keyColumn.activeKind === "profile"
                                    model: controller.profiles
                                    Accessible.name: i18n("Profile target")
                                    onActivated: controller.actionValue = currentText
                                    Accessible.onPressAction: popup.open()
                                    Accessible.onIncreaseAction: stepCombo(profileTargetBox, 1)
                                    Accessible.onDecreaseAction: stepCombo(profileTargetBox, -1)
                                }
                                ColumnLayout {
                                    Kirigami.FormData.label: i18n("Value")
                                    Layout.fillWidth: true
                                    visible: keyColumn.activeKind === "key" || keyColumn.activeKind === "shortcut"
                                             || keyColumn.activeKind === "command" || keyColumn.activeKind === "sequence"
                                             || keyColumn.activeKind === "repeat"
                                    spacing: 2

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: Kirigami.Units.smallSpacing

                                        QQC2.TextField {
                                            id: valueField
                                            Layout.fillWidth: true
                                            Accessible.name: i18n("Action value")
                                            onTextChanged: if (text !== controller.actionValue) controller.actionValue = text
                                            placeholderText: {
                                                if (keyColumn.activeKind === "key")
                                                    return i18n("Key name, e.g. KP7, F5, ENTER")
                                                if (keyColumn.activeKind === "shortcut")
                                                    return i18n("Shortcut, e.g. CTRL+SHIFT+T")
                                                if (keyColumn.activeKind === "command")
                                                    return i18n("Command")
                                                if (keyColumn.activeKind === "sequence")
                                                    return i18n("Keys tapped in turn, e.g. UP DOWN RIGHT LEFT")
                                                if (keyColumn.activeKind === "repeat")
                                                    return i18n("Key or shortcut to repeat, e.g. SPACE, SHIFT+7")
                                                return ""
                                            }
                                        }
                                        QQC2.Button {
                                            text: i18n("Record")
                                            visible: keyColumn.activeKind === "key" || keyColumn.activeKind === "shortcut" || keyColumn.activeKind === "repeat"
                                            Accessible.name: i18n("Record")
                                            QQC2.ToolTip.visible: hovered
                                            QQC2.ToolTip.text: keyColumn.activeKind === "repeat"
                                                         ? i18n("Press the key or shortcut to repeat")
                                                         : i18n("Press the key or shortcut to use")
                                            onClicked: {
                                                let field = "key"
                                                if (keyColumn.activeKind === "shortcut")
                                                    field = "keys"
                                                else if (keyColumn.activeKind === "repeat")
                                                    field = "repeat"
                                                recordDialog.begin(field)
                                            }
                                        }
                                    }
                                }
                                RowLayout {
                                    Kirigami.FormData.label: i18n("Held throughout")
                                    Layout.fillWidth: true
                                    visible: keyColumn.activeKind === "sequence"
                                    spacing: Kirigami.Units.smallSpacing

                                    QQC2.TextField {
                                        id: holdField
                                        Layout.fillWidth: true
                                        Accessible.name: i18n("Held throughout")
                                        placeholderText: i18n("Optional, e.g. CTRL")
                                        onTextChanged: if (text !== controller.hold) controller.hold = text
                                    }
                                    QQC2.Button {
                                        text: i18n("Record")
                                        Accessible.name: i18n("Record hold")
                                        QQC2.ToolTip.visible: hovered
                                        QQC2.ToolTip.text: i18n("Press the key to hold")
                                        onClicked: recordDialog.begin("hold")
                                    }
                                }
                                QQC2.SpinBox {
                                    id: stepSpin
                                    Kirigami.FormData.label: i18n("Delay between taps (ms)")
                                    visible: keyColumn.activeKind === "sequence"
                                    from: 10
                                    to: 500
                                    stepSize: 5
                                    editable: true
                                    Accessible.name: i18n("Delay between taps (ms)")
                                    onValueChanged: if (keyColumn.activeKind === "sequence" && value !== controller.stepMs) controller.stepMs = value
                                }
                                QQC2.SpinBox {
                                    id: repeatSpin
                                    Kirigami.FormData.label: i18n("Time between presses (ms)")
                                    visible: keyColumn.activeKind === "repeat"
                                    from: 20
                                    to: 10000
                                    stepSize: 10
                                    editable: true
                                    Accessible.name: i18n("Time between presses (ms)")
                                    onValueChanged: if (keyColumn.activeKind === "repeat" && value !== controller.repeatMs) controller.repeatMs = value
                                }
                                QQC2.Label {
                                    Layout.fillWidth: true
                                    visible: controller.actionKindId === "folder"
                                    wrapMode: Text.WordWrap
                                    text: i18n("Opens another 12 keys. The top-left key steps back. Hold that key for a second to return to the top.")
                                }
                                QQC2.Label {
                                    Layout.fillWidth: true
                                    visible: controller.actionKindId === "back"
                                    wrapMode: Text.WordWrap
                                    text: i18n("A tap leaves this folder. Hold the key for a second to return to the top. The right dial's press leaves too.")
                                }
                                QQC2.CheckBox {
                                    id: confirmBox
                                    Kirigami.FormData.label: i18n("Feedback")
                                    visible: keyColumn.activeKind === "command"
                                    text: i18n("Flash when it finishes")
                                    Accessible.name: i18n("Flash when it finishes")
                                    onToggled: {
                                        if (checked !== controller.confirm)
                                            controller.confirm = checked
                                    }
                                }
                                RowLayout {
                                    Kirigami.FormData.label: i18n("Application")
                                    Layout.fillWidth: true
                                    visible: keyColumn.activeKind === "app"
                                    spacing: Kirigami.Units.smallSpacing

                                    QQC2.Label {
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                        text: controller.appLabel.length > 0 ? controller.appLabel : i18n("No application chosen")
                                        color: controller.appLabel.length > 0 ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
                                        Accessible.name: i18n("Application")
                                        Accessible.description: text
                                    }
                                    QQC2.Button {
                                        text: i18n("Choose…")
                                        Accessible.name: i18n("Choose application")
                                        onClicked: appDialog.open()
                                    }
                                }
                                QQC2.TextField {
                                    id: appArgsField
                                    Kirigami.FormData.label: i18n("Arguments")
                                    Layout.fillWidth: true
                                    visible: keyColumn.activeKind === "app"
                                    Accessible.name: i18n("Arguments")
                                    placeholderText: i18n("Extra arguments, for example --new-window")
                                    onTextChanged: if (text !== controller.appArgs) controller.appArgs = text
                                }
                                QQC2.Label {
                                    Layout.fillWidth: true
                                    visible: keyColumn.activeKind === "app"
                                    wrapMode: Text.WordWrap
                                    text: i18n("Starts the application directly. This is not a shell command.")
                                }
                            }
                        }
                    }

                    Flickable {
                        id: lookFlick

                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        contentWidth: width
                        contentHeight: lookColumn.implicitHeight
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds

                        ColumnLayout {
                            id: lookColumn
                            width: lookFlick.width
                            spacing: Kirigami.Units.smallSpacing

                            Kirigami.Heading {
                                level: 2
                                text: i18n("Theme")
                                Layout.fillWidth: true
                                Layout.topMargin: Kirigami.Units.largeSpacing
                                Layout.leftMargin: Kirigami.Units.largeSpacing
                            }

                            QQC2.Label {
                                Layout.fillWidth: true
                                Layout.leftMargin: Kirigami.Units.largeSpacing
                                Layout.rightMargin: Kirigami.Units.largeSpacing
                                wrapMode: Text.WordWrap
                                text: i18n("Edits save on their own. The deck picks them up within a second.")
                            }

                            QQC2.Label {
                                Layout.fillWidth: true
                                Layout.leftMargin: Kirigami.Units.largeSpacing
                                Layout.rightMargin: Kirigami.Units.largeSpacing
                                wrapMode: Text.WordWrap
                                text: controller.followTheme
                                      ? i18n("Uses the %1 theme and its colour tweaks. Pick a theme to give “%2” its own.", controller.followSource, controller.profile)
                                      : i18n("Used by the “%1” profile.", controller.profile)
                            }

                            GridLayout {
                                columns: 4
                                columnSpacing: Kirigami.Units.smallSpacing
                                rowSpacing: Kirigami.Units.smallSpacing
                                Layout.fillWidth: true
                                Layout.leftMargin: Kirigami.Units.largeSpacing
                                Layout.rightMargin: Kirigami.Units.largeSpacing

                                Repeater {
                                    model: controller.themes

                                    QQC2.Button {
                                        id: themeButton

                                        required property string modelData

                                        Layout.preferredWidth: 116
                                        Layout.preferredHeight: 132
                                        padding: 4
                                        onClicked: controller.theme = modelData

                                        background: Rectangle {
                                            radius: 10
                                            color: "transparent"
                                            border.width: controller.theme === themeButton.modelData ? 3 : 1
                                            border.color: controller.theme === themeButton.modelData ? Kirigami.Theme.highlightColor : Kirigami.Theme.separatorColor
                                        }

                                        contentItem: Column {
                                            spacing: 2

                                            Image {
                                                width: parent.width
                                                height: width
                                                source: "image://keys/theme/" + themeButton.modelData + "/" + controller.revision
                                                fillMode: Image.PreserveAspectFit
                                                cache: false
                                                smooth: true
                                            }
                                            QQC2.Label {
                                                width: parent.width
                                                horizontalAlignment: Text.AlignHCenter
                                                elide: Text.ElideRight
                                                text: themeButton.modelData
                                            }
                                        }
                                    }
                                }
                            }

                            Kirigami.Heading {
                                level: 2
                                text: i18n("Theme from wallpaper")
                                Layout.fillWidth: true
                                Layout.topMargin: Kirigami.Units.largeSpacing
                                Layout.leftMargin: Kirigami.Units.largeSpacing
                            }
                            QQC2.Label {
                                Layout.fillWidth: true
                                Layout.leftMargin: Kirigami.Units.largeSpacing
                                Layout.rightMargin: Kirigami.Units.largeSpacing
                                wrapMode: Text.WordWrap
                                text: i18n("Builds a “wallpaper” theme from the wallpaper you have up: its most vivid colours for text and accents, a dark tint of it for the background.")
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.leftMargin: Kirigami.Units.largeSpacing
                                Layout.rightMargin: Kirigami.Units.largeSpacing

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    QQC2.Label {
                                        text: i18n("Generate now")
                                        Layout.fillWidth: true
                                    }
                                    QQC2.Label {
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        color: Kirigami.Theme.disabledTextColor
                                        text: controller.wallpaperHint.length
                                              ? controller.wallpaperHint
                                              : i18n("No wallpaper found — set the wallpaper command in Settings")
                                        Accessible.name: i18n("Wallpaper source")
                                    }
                                }
                                QQC2.Button {
                                    text: i18n("Generate & use")
                                    Accessible.name: i18n("Generate & use")
                                    onClicked: controller.generateWallpaperTheme()
                                }
                            }
                            QQC2.CheckBox {
                                id: wallpaperAutoBox
                                Layout.leftMargin: Kirigami.Units.largeSpacing
                                text: i18n("Follow wallpaper changes")
                                Accessible.name: i18n("Follow wallpaper changes")
                                onToggled: {
                                    if (checked !== controller.wallpaperAuto)
                                        controller.wallpaperAuto = checked
                                }
                            }
                            QQC2.Label {
                                Layout.fillWidth: true
                                Layout.leftMargin: Kirigami.Units.largeSpacing
                                Layout.rightMargin: Kirigami.Units.largeSpacing
                                wrapMode: Text.WordWrap
                                color: Kirigami.Theme.disabledTextColor
                                text: i18n("Regenerate within ~10 s of a wallpaper change")
                            }

                            Kirigami.Heading {
                                level: 2
                                text: i18n("Colour tweaks")
                                Layout.fillWidth: true
                                Layout.topMargin: Kirigami.Units.largeSpacing
                                Layout.leftMargin: Kirigami.Units.largeSpacing
                            }
                            QQC2.Label {
                                Layout.fillWidth: true
                                Layout.leftMargin: Kirigami.Units.largeSpacing
                                Layout.rightMargin: Kirigami.Units.largeSpacing
                                wrapMode: Text.WordWrap
                                text: i18n("Change single colours of the theme for this profile only.")
                            }
                            Repeater {
                                model: [
                                    { field: "bg", title: i18n("Screen background") },
                                    { field: "key_bg", title: i18n("Key background") },
                                    { field: "fg", title: i18n("Text and icons") },
                                    { field: "accent", title: i18n("Highlight") },
                                    { field: "frame", title: i18n("Screen frame") },
                                    { field: "dim", title: i18n("Secondary text") },
                                    { field: "border", title: i18n("Key borders") }
                                ]
                                delegate: RowLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Layout.leftMargin: Kirigami.Units.largeSpacing
                                    Layout.rightMargin: Kirigami.Units.largeSpacing

                                    ColumnLayout {
                                        Layout.preferredWidth: 150
                                        spacing: 0
                                        QQC2.Label {
                                            text: modelData.title
                                            Layout.fillWidth: true
                                            wrapMode: Text.WordWrap
                                        }
                                        QQC2.Label {
                                            Layout.fillWidth: true
                                            color: Kirigami.Theme.disabledTextColor
                                            text: (controller.revision >= 0 && controller.themeTweaked(modelData.field)) ? i18n("Tweaked") : i18n("From theme")
                                            Accessible.name: modelData.title + " " + text
                                        }
                                    }
                                    QQC2.TextField {
                                        id: hexField
                                        Layout.fillWidth: true
                                        placeholderText: i18n("#rrggbb")
                                        Accessible.name: modelData.title
                                        onTextChanged: {
                                            if (text !== controller.themeColour(modelData.field))
                                                controller.setThemeColour(modelData.field, text)
                                        }
                                        Component.onCompleted: text = controller.themeColour(modelData.field)
                                        Connections {
                                            target: controller
                                            function onChanged() {
                                                if (!hexField.activeFocus)
                                                    hexField.text = controller.themeColour(modelData.field)
                                            }
                                        }
                                    }
                                    QQC2.Button {
                                        Accessible.name: i18n("Pick %1", modelData.title)
                                        implicitWidth: 36
                                        implicitHeight: 36
                                        onClicked: themeColourDialog.pick(modelData.field, controller.themeColour(modelData.field))
                                        contentItem: Rectangle {
                                            implicitWidth: 22
                                            implicitHeight: 22
                                            radius: 4
                                            border.width: 1
                                            border.color: Kirigami.Theme.separatorColor
                                            color: controller.revision >= 0 ? controller.themeColour(modelData.field) : "#888888"
                                        }
                                    }
                                    QQC2.Button {
                                        icon.name: "edit-undo"
                                        Accessible.name: i18n("Reset %1", modelData.title)
                                        QQC2.ToolTip.text: i18n("Back to the theme's colour")
                                        QQC2.ToolTip.visible: hovered
                                        onClicked: controller.resetThemeColour(modelData.field)
                                    }
                                }
                            }

                            Kirigami.FormLayout {
                                wideMode: false
                                Layout.fillWidth: true
                                Layout.maximumWidth: Math.min(lookFlick.width, Kirigami.Units.gridUnit * 32)
                                clip: true

                                Kirigami.Separator {
                                    Kirigami.FormData.isSection: true
                                    Kirigami.FormData.label: i18n("Pages")
                                }
                                QQC2.ComboBox {
                                    id: startPageBox
                                    Kirigami.FormData.label: i18n("Start page")
                                    Layout.fillWidth: true
                                    model: controller.pages
                                    onActivated: controller.startPage = currentText
                                }
                                QQC2.Label {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    text: i18n("Shown first, and where a right-dial press returns")
                                }
                                QQC2.CheckBox {
                                    id: settingsBox
                                    Kirigami.FormData.label: i18n("Settings page")
                                    text: i18n("Generated last page: themes, brightness, and profiles")
                                    onToggled: {
                                        if (checked !== controller.settingsPage)
                                            controller.settingsPage = checked
                                    }
                                }
                            }

                            Kirigami.Heading {
                                level: 2
                                text: i18n("Profile logo")
                                Layout.fillWidth: true
                                Layout.topMargin: Kirigami.Units.largeSpacing
                                Layout.leftMargin: Kirigami.Units.largeSpacing
                            }
                            QQC2.Label {
                                Layout.fillWidth: true
                                Layout.leftMargin: Kirigami.Units.largeSpacing
                                Layout.rightMargin: Kirigami.Units.largeSpacing
                                wrapMode: Text.WordWrap
                                text: i18n("Shown on the top screen when the deck switches to this profile. Without one, the profile's name is used.")
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.leftMargin: Kirigami.Units.largeSpacing
                                Layout.rightMargin: Kirigami.Units.largeSpacing

                                QQC2.TextField {
                                    id: logoField
                                    Layout.fillWidth: true
                                    placeholderText: i18n("The profile's name")
                                    Accessible.name: i18n("Logo path")
                                    onTextChanged: if (text !== controller.logo) controller.logo = text
                                }
                                QQC2.Button {
                                    text: i18n("Choose…")
                                    Accessible.name: i18n("Choose logo")
                                    onClicked: logoDialog.open()
                                }
                                QQC2.Button {
                                    text: i18n("Clear")
                                    Accessible.name: i18n("Clear logo")
                                    enabled: controller.logo.length > 0
                                    onClicked: controller.logo = ""
                                }
                            }
                        }
                    }

                    Flickable {
                        id: settingsFlick

                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        contentWidth: width
                        contentHeight: settingsColumn.implicitHeight
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds

                        ColumnLayout {
                            id: settingsColumn
                            width: settingsFlick.width
                            spacing: Kirigami.Units.smallSpacing

                            Kirigami.Heading {
                                level: 2
                                text: i18n("Deck")
                                Layout.fillWidth: true
                                Layout.topMargin: Kirigami.Units.largeSpacing
                                Layout.leftMargin: Kirigami.Units.largeSpacing
                            }

                            QQC2.Label {
                                Layout.fillWidth: true
                                Layout.leftMargin: Kirigami.Units.largeSpacing
                                Layout.rightMargin: Kirigami.Units.largeSpacing
                                wrapMode: Text.WordWrap
                                text: i18n("These save straight away. The running service reloads them within a second.")
                            }

                            Kirigami.FormLayout {
                                wideMode: false
                                Layout.fillWidth: true
                                Layout.maximumWidth: Math.min(settingsFlick.width, Kirigami.Units.gridUnit * 32)
                                clip: true

                                RowLayout {
                                    Kirigami.FormData.label: i18n("Brightness")
                                    Layout.fillWidth: true

                                    QQC2.Slider {
                                        id: brightnessSlider
                                        Layout.fillWidth: true
                                        from: 10
                                        to: 100
                                        stepSize: 5
                                        onMoved: controller.brightness = Math.round(value)
                                    }
                                    QQC2.Label {
                                        Layout.preferredWidth: 32
                                        horizontalAlignment: Text.AlignRight
                                        text: Math.round(brightnessSlider.value)
                                    }
                                }
                                ColumnLayout {
                                    Kirigami.FormData.label: i18n("Volume step")
                                    spacing: 2

                                    QQC2.SpinBox {
                                        id: volumeSpin
                                        from: 1
                                        to: 10
                                        onValueModified: controller.volumeStep = value
                                    }
                                    QQC2.Label {
                                        text: i18n("Percent per click of the left dial")
                                    }
                                }
                                QQC2.TextField {
                                    id: clockField
                                    Kirigami.FormData.label: i18n("Clock format")
                                    Layout.fillWidth: true
                                    placeholderText: i18n("strftime, such as %H:%M")
                                    onEditingFinished: controller.clockFormat = text
                                }
                                QQC2.ComboBox {
                                    id: startProfileBox
                                    Kirigami.FormData.label: i18n("Start profile")
                                    Layout.fillWidth: true
                                    model: controller.profiles
                                    onActivated: controller.startProfile = currentText
                                }

                                Kirigami.Separator {
                                    Kirigami.FormData.isSection: true
                                    Kirigami.FormData.label: i18n("Now playing")
                                }
                                QQC2.Label {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    text: i18n("Check a player to show only that player on the clock screen. Anything else is ignored. Leave them all unchecked to follow whichever player is active.")
                                }
                                ColumnLayout {
                                    Kirigami.FormData.label: i18n("Lock to")
                                    Layout.fillWidth: true
                                    spacing: 0

                                    Repeater {
                                        model: controller.playerChoices
                                        delegate: QQC2.CheckBox {
                                            required property string modelData
                                            text: modelData
                                            Accessible.name: i18n("Lock now playing to %1", modelData)
                                            checked: controller.mediaPlayers.indexOf(modelData) >= 0
                                            onToggled: {
                                                const locked = controller.mediaPlayers.indexOf(modelData) >= 0
                                                if (checked !== locked)
                                                    controller.setMediaPlayerEnabled(modelData, checked)
                                            }
                                        }
                                    }
                                    QQC2.Label {
                                        visible: controller.playerChoices.length === 0
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        text: i18n("No players are running. Add a name to lock one ahead of time.")
                                    }
                                }
                                RowLayout {
                                    Kirigami.FormData.label: i18n("Add a player")
                                    Layout.fillWidth: true

                                    QQC2.TextField {
                                        id: playerField
                                        Layout.fillWidth: true
                                        placeholderText: i18n("spotify, or a pattern such as firefox*")
                                        Accessible.name: i18n("Player name")
                                        onAccepted: window.addMediaPlayer()
                                    }
                                    QQC2.Button {
                                        text: i18n("Add")
                                        Accessible.name: i18n("Add player")
                                        enabled: playerField.text.trim().length > 0
                                        onClicked: window.addMediaPlayer()
                                    }
                                }

                                Kirigami.Separator {
                                    Kirigami.FormData.isSection: true
                                    Kirigami.FormData.label: i18n("Wallpaper theme")
                                }
                                QQC2.TextField {
                                    id: wallpaperCommandField
                                    Kirigami.FormData.label: i18n("Wallpaper command")
                                    Layout.fillWidth: true
                                    placeholderText: i18n("Blank: detect noctalia, swww, hyprpaper, GNOME")
                                    Accessible.name: i18n("Wallpaper command")
                                    onTextChanged: if (text !== controller.wallpaperCommand) controller.wallpaperCommand = text
                                    onEditingFinished: controller.refreshWallpaperHint()
                                }
                                QQC2.Label {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    text: i18n("Deck text uses your System Settings font (%1). Boot text uses your fixed-width font (%2).",
                                               controller.textFontFamily, controller.fixedFontFamily)
                                    Accessible.name: text
                                }

                                Kirigami.Separator {
                                    Kirigami.FormData.isSection: true
                                    Kirigami.FormData.label: i18n("Profile switch")
                                }
                                QQC2.Label {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    text: i18n("A short glitch when the deck changes profile: the new profile's logo resolves on the top screen while its keys come out of the interference. Set each profile's logo under Look.")
                                }
                                QQC2.CheckBox {
                                    id: transitionEnabledBox
                                    Kirigami.FormData.label: i18n("Animate")
                                    text: i18n("Animate")
                                    Accessible.name: i18n("Animate profile switch")
                                    onToggled: {
                                        if (checked !== controller.transitionEnabled)
                                            controller.transitionEnabled = checked
                                    }
                                }
                                QQC2.TextField {
                                    id: transitionSpeedField
                                    Kirigami.FormData.label: i18n("Speed")
                                    Layout.fillWidth: true
                                    Accessible.name: i18n("Transition speed")
                                    placeholderText: i18n("1.0 is about half a second")
                                    onTextChanged: Qt.callLater(function() {
                                        commitNumber(transitionSpeedField, controller.transitionSpeed, function(value) {
                                            controller.transitionSpeed = value
                                        })
                                    })
                                }
                                QQC2.TextField {
                                    id: transitionHoldField
                                    Kirigami.FormData.label: i18n("Hold the logo")
                                    Layout.fillWidth: true
                                    Accessible.name: i18n("Transition hold")
                                    placeholderText: i18n("Seconds it stays up once it has resolved")
                                    onTextChanged: Qt.callLater(function() {
                                        commitNumber(transitionHoldField, controller.transitionHold, function(value) {
                                            controller.transitionHold = value
                                        })
                                    })
                                }

                                Kirigami.Separator {
                                    Kirigami.FormData.isSection: true
                                    Kirigami.FormData.label: i18n("Boot animation")
                                }
                                QQC2.Label {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    text: i18n("Plays at login and whenever the keyboard is plugged in.")
                                }
                                RowLayout {
                                    QQC2.Button {
                                        text: i18n("Play now")
                                        Accessible.name: i18n("Play boot animation")
                                        onClicked: controller.replayBoot()
                                    }
                                }
                                QQC2.CheckBox {
                                    id: bootEnabledBox
                                    Kirigami.FormData.label: i18n("Enabled")
                                    text: i18n("Enabled")
                                    Accessible.name: i18n("Boot animation enabled")
                                    onToggled: {
                                        if (checked !== controller.bootEnabled)
                                            controller.bootEnabled = checked
                                    }
                                }
                                QQC2.TextField {
                                    id: bootSpeedField
                                    Kirigami.FormData.label: i18n("Boot speed")
                                    Layout.fillWidth: true
                                    Accessible.name: i18n("Boot speed")
                                    placeholderText: i18n("1.0 is about 5 seconds; 0.5 is half speed")
                                    onTextChanged: Qt.callLater(function() {
                                        commitNumber(bootSpeedField, controller.bootSpeed, function(value) {
                                            controller.bootSpeed = value
                                        })
                                    })
                                }
                                QQC2.TextField {
                                    id: bootTitleField
                                    Kirigami.FormData.label: i18n("Title")
                                    Layout.fillWidth: true
                                    Accessible.name: i18n("Boot title")
                                    onTextChanged: Qt.callLater(function() {
                                        commitString(bootTitleField, controller.bootTitle, function(value) {
                                            controller.bootTitle = value
                                        })
                                    })
                                }
                                QQC2.TextField {
                                    id: bootLinesField
                                    Kirigami.FormData.label: i18n("Status lines")
                                    Layout.fillWidth: true
                                    Accessible.name: i18n("Boot lines")
                                    placeholderText: i18n("LABEL|STATUS, separated by ;")
                                    onTextChanged: Qt.callLater(function() {
                                        commitString(bootLinesField, controller.bootLinesText, function(value) {
                                            controller.bootLinesText = value
                                        })
                                    })
                                }
                                RowLayout {
                                    Kirigami.FormData.label: i18n("Boot logo")
                                    Layout.fillWidth: true
                                    QQC2.TextField {
                                        id: bootLogoField
                                        Layout.fillWidth: true
                                        placeholderText: i18n("None")
                                        Accessible.name: i18n("Boot logo")
                                        onTextChanged: Qt.callLater(function() {
                                            commitString(bootLogoField, controller.bootLogo, function(value) {
                                                controller.bootLogo = value
                                            })
                                        })
                                    }
                                    QQC2.Button {
                                        text: i18n("Choose…")
                                        Accessible.name: i18n("Choose boot logo")
                                        onClicked: bootLogoDialog.open()
                                    }
                                    QQC2.Button {
                                        text: i18n("Clear")
                                        Accessible.name: i18n("Clear boot logo")
                                        enabled: controller.bootLogo.length > 0
                                        onClicked: controller.bootLogo = ""
                                    }
                                }
                                RowLayout {
                                    Kirigami.FormData.label: i18n("Boot animation file")
                                    Layout.fillWidth: true
                                    QQC2.TextField {
                                        id: bootGifField
                                        Layout.fillWidth: true
                                        placeholderText: i18n("None")
                                        Accessible.name: i18n("Boot gif")
                                        onTextChanged: Qt.callLater(function() {
                                            commitString(bootGifField, controller.bootGif, function(value) {
                                                controller.bootGif = value
                                            })
                                        })
                                    }
                                    QQC2.Button {
                                        text: i18n("Choose…")
                                        Accessible.name: i18n("Choose boot gif")
                                        onClicked: bootGifDialog.open()
                                    }
                                    QQC2.Button {
                                        text: i18n("Clear")
                                        Accessible.name: i18n("Clear boot gif")
                                        enabled: controller.bootGif.length > 0
                                        onClicked: controller.bootGif = ""
                                    }
                                }

                                Kirigami.Separator {
                                    Kirigami.FormData.isSection: true
                                    Kirigami.FormData.label: i18n("Auto-switch profiles")
                                }
                                QQC2.Label {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    text: controller.focusHint
                                }
                                QQC2.Button {
                                    text: i18n("Add rule")
                                    Accessible.name: i18n("Add rule")
                                    onClicked: controller.addSwitchRule()
                                }
                                QQC2.ComboBox {
                                    id: fallbackBox
                                    Kirigami.FormData.label: i18n("Other windows")
                                    Layout.fillWidth: true
                                    model: [i18n("(stay on current)")].concat(controller.profiles)
                                    Accessible.name: i18n("Other windows")
                                    onActivated: function(selected) {
                                        const name = selected <= 0 ? "" : textAt(selected)
                                        if (name !== controller.autoFallback)
                                            controller.autoFallback = name
                                    }
                                    Accessible.onPressAction: popup.open()
                                    Accessible.onIncreaseAction: stepCombo(fallbackBox, 1)
                                    Accessible.onDecreaseAction: stepCombo(fallbackBox, -1)
                                }
                                Repeater {
                                    model: controller.profileSwitchRules
                                    delegate: ColumnLayout {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        spacing: 0
                                        QQC2.Label {
                                            Layout.fillWidth: true
                                            wrapMode: Text.WordWrap
                                            text: modelData.text
                                            Accessible.name: modelData.text
                                        }
                                        QQC2.Label {
                                            Layout.fillWidth: true
                                            wrapMode: Text.WordWrap
                                            opacity: 0.7
                                            text: modelData.subtitle
                                        }
                                    }
                                }
                                Repeater {
                                    model: controller.switchRules
                                    delegate: RowLayout {
                                        required property var modelData
                                        required property int index
                                        Layout.fillWidth: true
                                        QQC2.TextField {
                                            id: ruleClassField
                                            Layout.fillWidth: true
                                            text: modelData.pattern
                                            Accessible.name: i18n("Rule class %1", index + 1)
                                            onTextChanged: {
                                                const row = index
                                                const field = ruleClassField
                                                Qt.callLater(function() {
                                                    const rules = controller.switchRules
                                                    if (row < 0 || row >= rules.length)
                                                        return
                                                    if (field.text !== rules[row].pattern)
                                                        controller.setSwitchRuleClass(row, field.text)
                                                })
                                            }
                                        }
                                        QQC2.ComboBox {
                                            id: ruleProfileBox
                                            Layout.preferredWidth: 180
                                            model: controller.profiles
                                            Accessible.name: i18n("Rule profile %1", index + 1)
                                            // A binding on currentIndex snaps the popup selection back
                                            // before activated can save it.
                                            Component.onCompleted: currentIndex = Math.max(0, controller.profiles.indexOf(modelData.profile))
                                            onActivated: function(selected) {
                                                controller.setSwitchRuleProfile(index, textAt(selected))
                                            }
                                            Accessible.onPressAction: popup.open()
                                            Accessible.onIncreaseAction: stepCombo(ruleProfileBox, 1)
                                            Accessible.onDecreaseAction: stepCombo(ruleProfileBox, -1)
                                        }
                                        QQC2.Button {
                                            text: i18n("Remove")
                                            Accessible.name: i18n("Remove rule %1", index + 1)
                                            onClicked: controller.removeSwitchRule(index)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Flickable {
                        id: addonFlick

                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        contentWidth: width
                        contentHeight: addonColumn.implicitHeight
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds

                        ColumnLayout {
                            id: addonColumn
                            width: addonFlick.width
                            spacing: Kirigami.Units.smallSpacing

                            Kirigami.Heading {
                                level: 2
                                text: i18n("Add-ons")
                                Layout.fillWidth: true
                                Layout.topMargin: Kirigami.Units.largeSpacing
                                Layout.leftMargin: Kirigami.Units.largeSpacing
                            }

                            QQC2.Label {
                                Layout.fillWidth: true
                                Layout.leftMargin: Kirigami.Units.largeSpacing
                                Layout.rightMargin: Kirigami.Units.largeSpacing
                                wrapMode: Text.WordWrap
                                text: controller.addonStatus.length ? controller.addonStatus
                                      : i18n("Profiles, themes, and images from galleon-addon packages. The deck reloads them within a second.")
                            }

                            RowLayout {
                                Layout.leftMargin: Kirigami.Units.largeSpacing
                                Layout.rightMargin: Kirigami.Units.largeSpacing

                                QQC2.Button {
                                    text: i18n("Install package")
                                    icon.name: "document-import"
                                    onClicked: addonDialog.open()
                                }
                                QQC2.Button {
                                    text: i18n("Get add-ons")
                                    icon.name: "globe"
                                    onClicked: Qt.openUrlExternally("https://github.com/NLMP-DDHS/galleon-deck-addons/releases")
                                }
                                QQC2.Button {
                                    text: i18n("Refresh")
                                    icon.name: "view-refresh"
                                    onClicked: controller.refreshAddons()
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.leftMargin: Kirigami.Units.largeSpacing
                                Layout.rightMargin: Kirigami.Units.largeSpacing

                                QQC2.Label {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    text: controller.addonDir.length
                                          ? i18n("Add-on folder: %1", controller.addonDir)
                                          : i18n("Add-on folder: also list a checkout. Each add-on is a subfolder with an addon.toml.")
                                }
                                QQC2.Button {
                                    text: i18n("Choose")
                                    icon.name: "folder-open"
                                    onClicked: addonFolderDialog.open()
                                }
                                QQC2.Button {
                                    visible: controller.addonDir.length > 0
                                    text: i18n("Clear")
                                    icon.name: "edit-clear"
                                    onClicked: controller.clearAddonDir()
                                }
                            }

                            Repeater {
                                model: controller.addons

                                ColumnLayout {
                                    id: addonCard
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Layout.leftMargin: Kirigami.Units.largeSpacing
                                    Layout.rightMargin: Kirigami.Units.largeSpacing
                                    spacing: 2

                                    Kirigami.Heading {
                                        level: 4
                                        text: modelData.title + "  " + modelData.version
                                        Layout.fillWidth: true
                                    }
                                    QQC2.Label {
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        text: modelData.description
                                    }
                                    QQC2.Label {
                                        Layout.fillWidth: true
                                        color: Kirigami.Theme.disabledTextColor
                                        text: modelData.compatible === false
                                              ? i18n("Needs galleon-deck %1", modelData.requires)
                                              : (modelData.installed ? i18n("Installed %1", modelData.installed) : i18n("Not installed"))
                                    }
                                    RowLayout {
                                        QQC2.Button {
                                            visible: !modelData.installed || modelData.upgrade
                                            text: modelData.installed ? i18n("Upgrade") : i18n("Install")
                                            enabled: modelData.compatible !== false
                                            onClicked: controller.installAddon(modelData.name)
                                        }
                                        QQC2.Button {
                                            visible: !!modelData.installed
                                            text: i18n("Remove")
                                            onClicked: {
                                                removeDialog.addonName = modelData.name
                                                removeDialog.addonTitle = modelData.title
                                                removeDialog.open()
                                            }
                                        }
                                    }
                                    Repeater {
                                        model: addonCard.modelData.profiles
                                        delegate: QQC2.Button {
                                            required property string modelData
                                            visible: controller.profiles.indexOf(modelData) !== -1
                                            text: i18n("Open profile %1", modelData)
                                            icon.name: "go-next"
                                            onClicked: {
                                                controller.profile = modelData
                                                sections.currentIndex = 0
                                            }
                                        }
                                    }
                                    Repeater {
                                        model: addonCard.modelData.commands
                                        delegate: QQC2.Button {
                                            required property string modelData
                                            text: i18n("Run %1", modelData)
                                            onClicked: controller.runAddon(addonCard.modelData.name, modelData)
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

    QQC2.Menu {
        id: serviceMenu
        QQC2.MenuItem {
            text: i18n("Replay boot animation")
            icon.name: "media-playback-start"
            onTriggered: controller.replayBoot()
        }
        QQC2.MenuItem {
            text: i18n("Restart service")
            icon.name: "system-reboot"
            onTriggered: controller.restartService()
        }
    }

    QQC2.Menu {
        id: profileMenu
        QQC2.MenuItem {
            text: i18n("New profile…")
            onTriggered: nameDialog.ask("newProfile", i18n("New profile"), i18n("Name for the profile:"), "")
        }
        QQC2.MenuItem {
            text: i18n("Duplicate profile…")
            onTriggered: nameDialog.ask("duplicateProfile", i18n("Duplicate profile"), i18n("Name for the profile:"), controller.profile + "-copy")
        }
        QQC2.MenuItem {
            text: i18n("Rename profile…")
            onTriggered: nameDialog.ask("renameProfile", i18n("Rename profile"), i18n("New name:"), controller.profile)
        }
        QQC2.MenuItem {
            text: i18n("Delete profile…")
            onTriggered: confirmDialog.ask("profile", i18n("Delete profile “%1”?", controller.profile), i18n("Its file is deleted; this can't be undone."))
        }
        QQC2.MenuItem {
            text: i18n("Import Stream Deck profile…")
            onTriggered: importDialog.open()
        }
        QQC2.MenuItem {
            text: i18n("Start with this profile")
            onTriggered: controller.makeStartProfile()
        }
    }

    QQC2.Menu {
        id: pageMenu
        QQC2.MenuItem {
            text: i18n("Add page…")
            onTriggered: nameDialog.ask("addPage", i18n("New page"), i18n("Name for the page:"), "")
        }
        QQC2.MenuItem {
            text: i18n("Rename page…")
            onTriggered: nameDialog.ask("renamePage", i18n("Rename page"), i18n("New name:"), controller.pageName)
        }
        QQC2.MenuItem {
            text: i18n("Delete page…")
            onTriggered: confirmDialog.ask("page", i18n("Delete page “%1”?", controller.pageName), i18n("Its keys are removed too."))
        }
        QQC2.MenuItem {
            text: i18n("Move page left")
            onTriggered: controller.movePage(-1)
        }
        QQC2.MenuItem {
            text: i18n("Move page right")
            onTriggered: controller.movePage(1)
        }
        QQC2.MenuItem {
            text: i18n("Make start page")
            onTriggered: controller.makeStartPage()
        }
    }

    QQC2.Dialog {
        id: recordDialog

        property string recordField: "key"

        modal: true
        width: 460
        title: recordField === "keys" || recordField === "repeat" ? i18n("Press the shortcut") : i18n("Press the key")
        standardButtons: QQC2.Dialog.Cancel
        anchors.centerIn: parent

        function begin(field) {
            recordField = field
            open()
        }

        onOpened: controller.beginRecord(recordField)
        onClosed: controller.cancelRecord()

        contentItem: QQC2.Label {
            wrapMode: Text.WordWrap
            text: i18n("Waiting for a key… (Escape cancels.) Shortcuts your desktop grabs, like SUPER combinations, may not reach this window.")
        }
    }

    QQC2.Dialog {
        id: iconDialog

        modal: true
        width: 560
        height: 560
        title: i18n("Choose an icon")
        standardButtons: QQC2.Dialog.Close
        anchors.centerIn: parent

        onOpened: {
            iconSearch.text = ""
            controller.ensureIcons()
        }

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            QQC2.TextField {
                id: iconSearch
                Layout.fillWidth: true
                placeholderText: i18n("Search icons, for example discord, play, folder")
                Accessible.name: i18n("Search icons")
                onTextChanged: controller.iconFilter = text
            }
            QQC2.Label {
                text: i18n("%1 icons", controller.iconCount)
                Layout.fillWidth: true
            }
            GridView {
                id: iconGrid
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: 420
                cellWidth: 92
                cellHeight: 72
                clip: true
                model: controller.iconCount
                boundsBehavior: Flickable.StopAtBounds

                delegate: QQC2.AbstractButton {
                    required property int index
                    width: 92
                    height: 72
                    Accessible.name: controller.iconName(index)
                    onClicked: {
                        controller.icon = controller.iconGlyph(index)
                        iconDialog.close()
                    }

                    contentItem: Column {
                        spacing: 2
                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: controller.iconGlyph(index)
                            font.family: "Symbols Nerd Font"
                            font.pixelSize: 26
                            color: Kirigami.Theme.textColor
                        }
                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                            text: controller.iconLabel(index)
                            color: Kirigami.Theme.textColor
                            font.pixelSize: 9
                        }
                    }
                }
            }
        }
    }

    QQC2.Dialog {
        id: aboutDialog
        title: i18n("Galleon Deck")
        modal: true
        width: 420
        standardButtons: QQC2.Dialog.Close
        anchors.centerIn: parent

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            QQC2.Label {
                text: i18n("Version 1.3.0")
            }
            QQC2.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: i18n("Stream Deck support for the Corsair Galleon 100 SD keyboard on Linux.")
            }
            QQC2.Label {
                text: i18n("galleon-deck contributors")
                color: Kirigami.Theme.disabledTextColor
            }
            QQC2.Label {
                text: i18n("MIT")
                color: Kirigami.Theme.disabledTextColor
            }
        }
    }

    QQC2.Dialog {
        id: appDialog
        title: i18n("Choose application")
        modal: true
        width: 440
        height: 520
        standardButtons: QQC2.Dialog.Cancel
        anchors.centerIn: parent

        function matches(app) {
            const query = appSearch.text.trim().toLowerCase()
            if (query.length === 0)
                return true
            const name = String(app.name || "").toLowerCase()
            const id = String(app.id || "").toLowerCase()
            return name.indexOf(query) >= 0 || id.indexOf(query) >= 0
        }

        onOpened: {
            controller.refreshApplications()
            if (appSearch.text.length > 0)
                appSearch.text = ""
            else
                appModel.rebuild()
            appSearch.forceActiveFocus()
        }

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            QQC2.TextField {
                id: appSearch
                Layout.fillWidth: true
                placeholderText: i18n("Search applications")
                Accessible.name: i18n("Search applications")
                onTextChanged: appModel.rebuild()
            }
            QQC2.Label {
                Layout.fillWidth: true
                visible: appList.count === 0
                color: Kirigami.Theme.disabledTextColor
                text: i18n("No matching applications")
            }
            ListView {
                id: appList
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: 400
                clip: true
                model: ListModel {
                    id: appModel
                    function rebuild() {
                        clear()
                        const apps = controller.applicationChoices
                        for (let i = 0; i < apps.length; ++i) {
                            if (!appDialog.matches(apps[i]))
                                continue
                            append({
                                appId: String(apps[i].id || ""),
                                appName: String(apps[i].name || ""),
                                iconName: String(apps[i].icon || "")
                            })
                        }
                    }
                }
                delegate: QQC2.ItemDelegate {
                    required property int index
                    required property string appId
                    required property string appName
                    required property string iconName
                    width: appList.width
                    text: appName
                    icon.name: iconName.length === 0 || iconName.charAt(0) === "/" ? "application-x-executable" : iconName
                    icon.source: iconName.charAt(0) === "/" ? "file://" + encodeURI(iconName) : ""
                    Accessible.role: Accessible.Button
                    Accessible.name: appName
                    function choose() {
                        controller.appId = appId
                        appDialog.close()
                    }
                    onClicked: choose()
                    Accessible.onPressAction: choose()
                }
            }
        }
    }

    QQC2.Dialog {
        id: nameDialog

        property string kind
        property string prompt

        modal: true
        width: 420
        standardButtons: QQC2.Dialog.Ok | QQC2.Dialog.Cancel
        anchors.centerIn: parent

        function ask(nextKind, nextTitle, nextPrompt, initial) {
            kind = nextKind
            title = nextTitle
            prompt = nextPrompt
            nameField.text = initial
            open()
            nameField.forceActiveFocus()
            nameField.selectAll()
        }

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            QQC2.Label {
                text: nameDialog.prompt
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            QQC2.TextField {
                id: nameField
                Layout.fillWidth: true
                Accessible.name: i18n("Name")
                onAccepted: nameDialog.accept()
            }
        }

        onAccepted: {
            const entered = nameField.text
            if (kind === "addPage")
                controller.addPage(entered)
            else if (kind === "renamePage")
                controller.renamePage(entered)
            else if (kind === "newProfile")
                controller.newProfile(entered)
            else if (kind === "duplicateProfile")
                controller.duplicateProfile(entered)
            else if (kind === "renameProfile")
                controller.renameProfile(entered)
            else if (kind === "importProfile")
                controller.importStreamDeck(importDialog.selectedFile, entered)
        }
    }

    QQC2.Dialog {
        id: confirmDialog

        property string kind

        modal: true
        width: 420
        standardButtons: QQC2.Dialog.Ok | QQC2.Dialog.Cancel
        anchors.centerIn: parent

        function ask(nextKind, nextTitle, nextBody) {
            kind = nextKind
            title = nextTitle
            body.text = nextBody
            open()
            const ok = standardButton(QQC2.Dialog.Ok)
            if (ok)
                ok.text = i18n("Delete")
        }

        contentItem: QQC2.Label {
            id: body
            wrapMode: Text.WordWrap
        }

        onAccepted: {
            if (kind === "page")
                controller.deletePage()
            else if (kind === "profile")
                controller.deleteProfile()
        }
    }

    QQC2.Dialog {
        id: removeDialog
        property string addonName
        property string addonTitle
        title: i18n("Remove add-on")
        modal: true
        standardButtons: QQC2.Dialog.Ok | QQC2.Dialog.Cancel
        anchors.centerIn: parent

        contentItem: QQC2.Label {
            wrapMode: Text.WordWrap
            text: i18n("Remove %1? Its profile, including your changes, is backed up first.", removeDialog.addonTitle)
        }

        onAccepted: controller.removeAddon(addonName)
    }

    readonly property var colourModel: [i18n("Theme default"), "fg", "accent", "frame", "dim", "border", "key_bg", "bg", i18n("Custom")]

    function colourIndex(value) {
        const ids = ["", "fg", "accent", "frame", "dim", "border", "key_bg", "bg"]
        const index = ids.indexOf(value)
        return index < 0 ? ids.length : index
    }

    function colourChoice(index, current) {
        const ids = ["", "fg", "accent", "frame", "dim", "border", "key_bg", "bg"]
        if (index < ids.length)
            return ids[index]
        if (current && current.charAt(0) === "#")
            return current
        return controller.colourHex(current && current.length ? current : "fg")
    }

    Timer {
        interval: 1000
        running: true
        repeat: true
        onTriggered: controller.bumpPreview()
    }

    ColorDialog {
        id: themeColourDialog
        title: i18n("Colour")
        function pick(field, hex) {
            themeColourDialog.field = field
            selectedColor = hex
            open()
        }
        property string field
        onAccepted: controller.setThemeColour(field, String(selectedColor))
    }

    FileDialog {
        id: imageDialog
        title: i18n("Key image")
        nameFilters: [i18n("Images (*.png *.jpg *.jpeg *.webp *.gif *.apng)")]
        onAccepted: controller.setImageUrl(selectedFile)
    }

    FileDialog {
        id: logoDialog
        title: i18n("Choose a logo")
        nameFilters: [i18n("Images (*.png *.webp *.jpg *.jpeg)")]
        onAccepted: controller.setLogoUrl(selectedFile)
    }

    FileDialog {
        id: bootLogoDialog
        title: i18n("Choose file")
        nameFilters: [i18n("Images (*.png *.webp)")]
        onAccepted: controller.setBootLogoUrl(selectedFile)
    }

    FileDialog {
        id: bootGifDialog
        title: i18n("Choose file")
        nameFilters: [i18n("Animations (*.gif *.png *.webp)")]
        onAccepted: controller.setBootGifUrl(selectedFile)
    }

    FileDialog {
        id: addonDialog
        title: i18n("Add-on package")
        nameFilters: [i18n("Add-on packages (*.tar.gz *.tgz *.zip)")]
        onAccepted: controller.installAddonFile(selectedFile)
    }

    FileDialog {
        id: importDialog
        title: i18n("Import a Stream Deck profile")
        nameFilters: [i18n("Stream Deck profiles (*.streamDeckProfile *.zip)")]
        onAccepted: {
            const suggested = controller.peekStreamDeck(selectedFile)
            if (suggested.length > 0)
                nameDialog.ask("importProfile", i18n("Import Stream Deck profile"), i18n("Name for the profile:"), suggested)
        }
    }

    Labs.FolderDialog {
        id: addonFolderDialog
        title: i18n("Choose a folder of add-ons")
        onAccepted: controller.setAddonDir(folder)
    }
}
