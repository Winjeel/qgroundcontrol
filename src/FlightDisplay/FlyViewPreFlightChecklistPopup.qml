/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs

import QGroundControl

import QGroundControl.Controls

/// Popup container for preflight checklists
QGCPopupDialog {
    id:         _root
    title:      qsTr("Pre-Flight Checklist")
    buttons:    Dialog.Close

    property var    _activeVehicle:     QGroundControl.multiVehicleManager.activeVehicle
    property bool   _useChecklist:      QGroundControl.settingsManager.appSettings.useChecklist.rawValue && QGroundControl.corePlugin.options.preFlightChecklistUrl.toString().length
    property bool   _enforceChecklist:  _useChecklist && QGroundControl.settingsManager.appSettings.enforceChecklist.rawValue
    property bool   _checklistComplete: _activeVehicle && (_activeVehicle.checkListState === Vehicle.CheckListPassed)

    on_ActiveVehicleChanged: _showPreFlightChecklistIfNeeded()

    Connections {
        target:                             mainWindow
        onShowPreFlightChecklistIfNeeded:   _root._showPreFlightChecklistIfNeeded()
    }

    function _showPreFlightChecklistIfNeeded() {
        if (_activeVehicle && !_checklistComplete && _enforceChecklist) {
            popupTimer.restart()
        }
    }

    Timer {
        id:             popupTimer
        interval:       1000
        repeat:         false
        onTriggered: {
            if (!_checklistComplete) {
                _root.open()
            } else {
                _root.close()
            }
        }
    }

    Loader {
        id:     checkList
        source: QGroundControl.corePlugin.options.preFlightChecklistUrl
    }

    property alias checkListItem: checkList.item

    Connections {
        target: checkList.item
        onAllChecksPassedChanged: {
            if (target.allChecksPassed) {
                popupTimer.restart()
            }
        }
    }
}
