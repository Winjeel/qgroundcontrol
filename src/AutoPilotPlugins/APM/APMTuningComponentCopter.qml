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
import QtQuick.Layouts

import QGroundControl

import QGroundControl.FactControls

import QGroundControl.Controls
import QGroundControl.ScreenTools


SetupPage {
    id:             tuningPage
    pageComponent:  tuningPageComponent

    Component {
        id: tuningPageComponent

        Column {
            width: availableWidth
            height: availableHeight

            FactPanelController { id: controller; }

            property bool _atcInputTCAvailable: controller.parameterExists(-1, "ATC_INPUT_TC")
            property Fact _atcInputTC:          controller.getParameterFact(-1, "ATC_INPUT_TC", false)
            property Fact _rateRollP:           controller.getParameterFact(-1, "ATC_RAT_RLL_P")
            property Fact _rateRollI:           controller.getParameterFact(-1, "ATC_RAT_RLL_I")
            property Fact _ratePitchP:          controller.getParameterFact(-1, "ATC_RAT_PIT_P")
            property Fact _ratePitchI:          controller.getParameterFact(-1, "ATC_RAT_PIT_I")
            property Fact _rateClimbP:          controller.getParameterFact(-1, "PSC_ACCZ_P")
            property Fact _rateClimbI:          controller.getParameterFact(-1, "PSC_ACCZ_I")
            property Fact _motSpinArm:          controller.getParameterFact(-1, "MOT_SPIN_ARM")
            property Fact _motSpinMin:          controller.getParameterFact(-1, "MOT_SPIN_MIN")

            property Fact _ch7Opt:  controller.getParameterFact(-1, "r.RC7_OPTION")
            property Fact _ch8Opt:  controller.getParameterFact(-1, "r.RC8_OPTION")
            property Fact _ch9Opt:  controller.getParameterFact(-1, "r.RC9_OPTION")
            property Fact _ch10Opt: controller.getParameterFact(-1, "r.RC10_OPTION")
            property Fact _ch11Opt: controller.getParameterFact(-1, "r.RC11_OPTION")
            property Fact _ch12Opt: controller.getParameterFact(-1, "r.RC12_OPTION")

            readonly property int   _firstOptionChannel:    7
            readonly property int   _lastOptionChannel:     12

            property Fact   _autoTuneAxes:                  controller.getParameterFact(-1, "AUTOTUNE_AXES")
            property int    _autoTuneSwitchChannelIndex:    0
            readonly property int _autoTuneOption:          17

            property real _margins: ScreenTools.defaultFontPixelHeight

            property bool _loadComplete: false

            Component.onCompleted: {
                // We use QtCharts only on Desktop platforms
                showAdvanced = !ScreenTools.isMobile

                // Qml Sliders have a strange behavior in which they first set Slider::value to some internal
                // setting and then set Slider::value to the bound properties value. If you have an onValueChanged
                // handler which updates your property with the new value, this first value change will trash
                // your bound values. In order to work around this we don't set the values into the Sliders until
                // after Qml load is done. We also don't track value changes until Qml load completes.
                rollPitch.value = _rateRollP.value
                climb.value = _rateClimbP.value
                if (_atcInputTCAvailable) {
                    atcInputTC.value = _atcInputTC.value
                }
                _loadComplete = true

                calcAutoTuneChannel()
            }

            /// The AutoTune switch is stored in one of the RC#_OPTION parameters. We need to loop through those
            /// to find them and setup the ui accordindly.
            function calcAutoTuneChannel() {
                _autoTuneSwitchChannelIndex = 0
                for (var channel=_firstOptionChannel; channel<=_lastOptionChannel; channel++) {
                    var optionFact = controller.getParameterFact(-1, "r.RC" + channel + "_OPTION")
                    if (optionFact.value == _autoTuneOption) {
                        _autoTuneSwitchChannelIndex = channel - _firstOptionChannel + 1
                        break
                    }
                }
            }

            /// We need to clear AutoTune from any previous channel before setting it to a new one
            function setChannelAutoTuneOption(channel) {
                // First clear any previous settings for AutTune
                for (var optionChannel=_firstOptionChannel; optionChannel<=_lastOptionChannel; optionChannel++) {
                    var optionFact = controller.getParameterFact(-1, "r.RC" + optionChannel + "_OPTION")
                    if (optionFact.value == _autoTuneOption) {
                        optionFact.value = 0
                    }
                }

                // Now set the function into the new channel
                if (channel != 0) {
                    var optionFact = controller.getParameterFact(-1, "r.RC" + channel + "_OPTION")
                    optionFact.value = _autoTuneOption
                }
            }

            Connections { target: _ch7Opt; function onValueChanged(value) { calcAutoTuneChannel() } }
            Connections { target: _ch8Opt; function onValueChanged(value) { calcAutoTuneChannel() } }
            Connections { target: _ch9Opt; function onValueChanged(value) { calcAutoTuneChannel() } }
            Connections { target: _ch10Opt; function onValueChanged(value) { calcAutoTuneChannel() } }
            Connections { target: _ch11Opt; function onValueChanged(value) { calcAutoTuneChannel() } }
            Connections { target: _ch12Opt; function onValueChanged(value) { calcAutoTuneChannel() } }

            Column {
                anchors.left:       parent.left
                anchors.right:      parent.right
                spacing:            _margins
                visible:            !advanced

                QGCLabel {
                    text:       qsTr("Basic Tuning")
                    font.bold:   true
                }

                Rectangle {
                    id:                 basicTuningRect
                    anchors.left:       parent.left
                    anchors.right:      parent.right
                    height:             basicTuningColumn.y + basicTuningColumn.height + _margins
                    color:              qgcPal.windowShade

                    Column {
                        id:                 basicTuningColumn
                        anchors.margins:    _margins
                        anchors.left:       parent.left
                        anchors.right:      parent.right
                        anchors.top:        parent.top
                        spacing:            _margins

                        Column {
                            anchors.left:       parent.left
                            anchors.right:      parent.right

                            QGCLabel {
                                text:       qsTr("Roll/Pitch Sensitivity")
                                font.bold:   true
                            }

                            QGCLabel {
                                text: qsTr("Slide to the right if the copter is sluggish or slide to the left if the copter is twitchy")
                            }

                            QGCSlider {
                                id:                 rollPitch
                                anchors.left:       parent.left
                                anchors.right:      parent.right
                                from:       0.08
                                to:       0.4
                                stepSize:           0.01
                                tickmarksEnabled:   true

                                onValueChanged: {
                                    if (_loadComplete) {
                                        _rateRollP.value = value
                                        _rateRollI.value = value
                                        _ratePitchP.value = value
                                        _ratePitchI.value = value
                                    }
                                }
                            }
                        }

                        Column {
                            anchors.left:       parent.left
                            anchors.right:      parent.right

                            QGCLabel {
                                text:       qsTr("Climb Sensitivity")
                                font.bold:   true
                            }

                            QGCLabel {
                                text: qsTr("Slide to the right to climb more aggressively or slide to the left to climb more gently")
                            }

                            QGCSlider {
                                id:                 climb
                                anchors.left:       parent.left
                                anchors.right:      parent.right
                                from:       0.3
                                to:       1.0
                                stepSize:           0.02
                                tickmarksEnabled:   true
                                value:              _rateClimbP.value

                                onValueChanged: {
                                    if (_loadComplete) {
                                        _rateClimbP.value = value
                                        _rateClimbI.value = value * 2
                                    }
                                }
                            }
                        }

                        Column {
                            anchors.left:       parent.left
                            anchors.right:      parent.right
                            visible:            _atcInputTCAvailable

                            QGCLabel {
                                text:       qsTr("RC Roll/Pitch Feel")
                                font.bold:   true
                            }

                            QGCLabel {
                                text: qsTr("Slide to the left for soft control, slide to the right for crisp control")
                            }

                            QGCSlider {
                                id:                 atcInputTC
                                anchors.left:       parent.left
                                anchors.right:      parent.right
                                from:       _atcInputTC.min
                                to:       _atcInputTC.max
                                stepSize:           _atcInputTC.increment
                                tickmarksEnabled:   true

                                onValueChanged: {
                                    if (_loadComplete) {
                                        _atcInputTC.value = value
                                    }
                                }
                            }
                        }

                        Column {
                            anchors.left:       parent.left
                            anchors.right:      parent.right

                            QGCLabel {
                                text:       qsTr("Spin While Armed")
                                font.bold:   true
                            }

                            QGCLabel {
                                text: qsTr("Adjust the amount the motors spin to indicate armed")
                            }

                            QGCSlider {
                                anchors.left:       parent.left
                                anchors.right:      parent.right
                                from:       0
                                to:       Math.max(0.3, _motSpinArm.rawValue)
                                stepSize:           0.01
                                tickmarksEnabled:   true
                                value:              _motSpinArm.rawValue

                                onValueChanged: {
                                    if (_loadComplete) {
                                        _motSpinArm.rawValue = value
                                    }
                                }
                            }
                        }

                        Column {
                            anchors.left:       parent.left
                            anchors.right:      parent.right

                            QGCLabel {
                                text:       qsTr("Minimum Thrust")
                                font.bold:   true
                            }

                            QGCLabel {
                                text: qsTr("Adjust the minimum amount of thrust require for the vehicle to move")
                            }

                            QGCLabel {
                                text:       qsTr("Warning: This setting should be higher than 'Spin While Armed'")
                                color:      qgcPal.warningText
                                visible:    _motSpinMin.rawValue < _motSpinArm.rawValue
                            }

                            QGCSlider {
                                anchors.left:       parent.left
                                anchors.right:      parent.right
                                from:       0
                                to:       Math.max(0.3, _motSpinMin.rawValue)
                                stepSize:           0.01
                                tickmarksEnabled:   true
                                value:              _motSpinMin.rawValue

                                onValueChanged: {
                                    if (_loadComplete) {
                                        _motSpinMin.rawValue = value
                                    }
                                }
                            }
                        }
                    }
                } // Rectangle - Basic tuning

                Flow {
                    id:                 flowLayout
                    Layout.fillWidth:   true
                    spacing:            _margins

                    Rectangle {
                        height: autoTuneLabel.height + autoTuneRect.height
                        width:  autoTuneRect.width
                        color:  qgcPal.window

                        QGCLabel {
                            id:                 autoTuneLabel
                            text:               qsTr("AutoTune")
                            font.bold:          true
                        }

                        Rectangle {
                            id:             autoTuneRect
                            width:          autoTuneColumn.x + autoTuneColumn.width + _margins
                            height:         autoTuneColumn.y + autoTuneColumn.height + _margins
                            anchors.top:    autoTuneLabel.bottom
                            color:          qgcPal.windowShade

                            Column {
                                id:                 autoTuneColumn
                                anchors.margins:    _margins
                                anchors.left:       parent.left
                                anchors.top:        parent.top
                                spacing:            _margins

                                Row {
                                    spacing: _margins

                                    QGCLabel { text: qsTr("Axes to AutoTune:") }
                                    FactBitmask { fact: _autoTuneAxes }
                                }

                                Row {
                                    spacing:    _margins

                                    QGCLabel {
                                        anchors.baseline:   autoTuneChannelCombo.baseline
                                        text:               qsTr("Channel for AutoTune switch:")
                                    }

                                    QGCComboBox {
                                        id:             autoTuneChannelCombo
                                        width:          ScreenTools.defaultFontPixelWidth * 14
                                        model:          [qsTr("None"), qsTr("Channel 7"), qsTr("Channel 8"), qsTr("Channel 9"), qsTr("Channel 10"), qsTr("Channel 11"), qsTr("Channel 12") ]
                                        currentIndex:   _autoTuneSwitchChannelIndex

                                        onActivated: (index) => {
                                            var channel = index

                                            if (channel > 0) {
                                                channel += 6
                                            }
                                            setChannelAutoTuneOption(channel)
                                        }
                                    }
                                }
                            }
                        } // Rectangle - AutoTune
                    } // Rectangle - AutoTuneWrap

                    Rectangle {
                        height:     inFlightTuneLabel.height + channel6TuningOption.height
                        width:      channel6TuningOption.width
                        color:      qgcPal.window

                        QGCLabel {
                            id:                 inFlightTuneLabel
                            text:               qsTr("In Flight Tuning")
                            font.bold:          true
                        }

                        Rectangle {
                            id:             channel6TuningOption
                            width:          channel6TuningOptColumn.width + (_margins * 2)
                            height:         channel6TuningOptColumn.height + ScreenTools.defaultFontPixelHeight
                            anchors.top:    inFlightTuneLabel.bottom
                            color:          qgcPal.windowShade

                            Column {
                                id:                 channel6TuningOptColumn
                                anchors.margins:    ScreenTools.defaultFontPixelWidth
                                anchors.left:       parent.left
                                anchors.top:        parent.top
                                spacing:            ScreenTools.defaultFontPixelHeight

                                Row {
                                    spacing: ScreenTools.defaultFontPixelWidth
                                    property Fact nullFact: Fact { }

                                    QGCLabel {
                                        anchors.baseline:   optCombo.baseline
                                        text:               qsTr("RC Channel 6 Option (Tuning):")
                                        //color:            controller.channelOptionEnabled[modelData] ? "yellow" : qgcPal.text
                                    }

                                    FactComboBox {
                                        id:         optCombo
                                        width:      ScreenTools.defaultFontPixelWidth * 15
                                        fact:       controller.getParameterFact(-1, "TUNE")
                                        indexModel: false
                                    }
                                }

                                Row {
                                    spacing: ScreenTools.defaultFontPixelWidth
                                    property Fact nullFact: Fact { }

                                    QGCLabel {
                                        anchors.baseline:   tuneMinField.baseline
                                        text:               qsTr("Min:")
                                        //color:            controller.channelOptionEnabled[modelData] ? "yellow" : qgcPal.text
                                    }

                                    FactTextField {
                                        id:                 tuneMinField
                                        validator:          DoubleValidator {bottom: 0; top: 32767;}
                                        fact:               controller.getParameterFact(-1, "r.TUNE_MAX")
                                    }

                                    QGCLabel {
                                        anchors.baseline:   tuneMaxField.baseline
                                        text:               qsTr("Max:")
                                        //color:            controller.channelOptionEnabled[modelData] ? "yellow" : qgcPal.text
                                    }

                                    FactTextField {
                                        id:                 tuneMaxField
                                        validator:          DoubleValidator {bottom: 0; top: 32767;}
                                        fact:               controller.getParameterFact(-1, "r.TUNE_MIN")
                                    }
                                }
                            } // Column - Channel 6 Tuning option
                        } // Rectangle - Channel 6 Tuning options
                    } // Rectangle - Channel 6 Tuning options wrap
                } // Flow - Tune
            }

            Loader {
                anchors.left:       parent.left
                anchors.right:      parent.right
                sourceComponent:    advanced ? advancePageComponent : undefined
            }

            Component {
                id: advancePageComponent

                PIDTuning {
                    anchors.left:   parent.left
                    anchors.right:  parent.right
                    height: availableHeight

                    property var roll: QtObject {
                        property string name: qsTr("Roll")
                        property var plot: [
                            { name: "Response", value: globals.activeVehicle.rollRate.value },
                            { name: "Setpoint", value: globals.activeVehicle.setpoint.rollRate.value }
                        ]
                        property var params: ListModel {
                            ListElement {
                                title:          qsTr("Roll axis angle controller P gain")
                                param:          "ATC_ANG_RLL_P"
                                description:    ""
                                min:            3
                                max:            12
                                step:           1
                            }
                            ListElement {
                                title:          qsTr("Roll axis rate controller P gain")
                                param:          "ATC_RAT_RLL_P"
                                description:    ""
                                min:            0.001
                                max:            0.5
                                step:           0.025
                            }
                            ListElement {
                                title:          qsTr("Roll axis rate controller I gain")
                                param:          "ATC_RAT_RLL_I"
                                description:    ""
                                min:            0.01
                                max:            2
                                step:           0.05
                            }
                            ListElement {
                                title:          qsTr("Roll axis rate controller D gain")
                                param:          "ATC_RAT_RLL_D"
                                description:    ""
                                min:            0.0
                                max:            0.05
                                step:           0.001
                            }
                        }
                    }
                    property var pitch: QtObject {
                        property string name: qsTr("Pitch")
                        property var plot: [
                            { name: "Response", value: globals.activeVehicle.pitchRate.value },
                            { name: "Setpoint", value: globals.activeVehicle.setpoint.pitchRate.value }
                        ]
                        property var params: ListModel {
                            ListElement {
                                title:          qsTr("Pitch axis angle controller P gain")
                                param:          "ATC_ANG_PIT_P"
                                description:    ""
                                min:            3
                                max:            12
                                step:           1
                            }
                            ListElement {
                                title:          qsTr("Pitch axis rate controller P gain")
                                param:          "ATC_RAT_PIT_P"
                                description:    ""
                                min:            0.001
                                max:            0.5
                                step:           0.025
                            }
                            ListElement {
                                title:          qsTr("Pitch axis rate controller I gain")
                                param:          "ATC_RAT_PIT_I"
                                description:    ""
                                min:            0.01
                                max:            2
                                step:           0.05
                            }
                            ListElement {
                                title:          qsTr("Pitch axis rate controller D gain")
                                param:          "ATC_RAT_PIT_D"
                                description:    ""
                                min:            0.0
                                max:            0.05
                                step:           0.001
                            }
                        }
                    }
                    property var yaw: QtObject {
                        property string name: qsTr("Yaw")
                        property var plot: [
                            { name: "Response", value: globals.activeVehicle.yawRate.value },
                            { name: "Setpoint", value: globals.activeVehicle.setpoint.yawRate.value }
                        ]
                        property var params: ListModel {
                            ListElement {
                                title:          qsTr("Yaw axis angle controller P gain")
                                param:          "ATC_ANG_YAW_P"
                                description:    ""
                                min:            3
                                max:            12
                                step:           1
                            }
                            ListElement {
                                title:          qsTr("Yaw axis rate controller P gain")
                                param:          "ATC_RAT_YAW_P"
                                description:    ""
                                min:            0.1
                                max:            2.5
                                step:           0.05
                            }
                            ListElement {
                                title:          qsTr("Yaw axis rate controller I gain")
                                param:          "ATC_RAT_YAW_I"
                                description:    ""
                                min:            0.01
                                max:            1
                                step:           0.05
                            }
                        }
                    }
                    title: "Rate"
                    tuningMode: Vehicle.ModeDisabled
                    unit: "deg/s"
                    axis: [ roll, pitch, yaw ]
                    chartDisplaySec: 3
                }
            } // Component - Advanced Page
        } // Column
    } // Component
} // SetupView
