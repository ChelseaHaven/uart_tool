cmake_minimum_required(VERSION 3.21)
project(uart_tool VERSION 0.2 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.8 REQUIRED COMPONENTS Quick QuickControls2 SerialPort)
qt_standard_project_setup(REQUIRES 6.8)

# Reusable receive pipeline: Qt Core only, no serial / GUI / ROS dependency.
qt_add_library(receive_core STATIC
    src/core/bytesource.h
    src/core/receivebuffer.cpp src/core/receivebuffer.h
    src/core/byteformatter.cpp src/core/byteformatter.h
    src/application/receivesession.cpp src/application/receivesession.h)
target_include_directories(receive_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(receive_core PUBLIC Qt6::Core)

# Protocol choices and session state remain independent of the receive transport.
qt_add_library(protocol_core STATIC
    src/protocols/checksumrule.cpp src/protocols/checksumrule.h
    src/protocols/binarytemplates.cpp
    src/protocols/binarychannels.cpp src/protocols/binarychannels.h
    src/protocols/protocoltemplatestore.cpp src/protocols/protocoltemplatestore.h
    src/protocols/framestructure.cpp src/protocols/multiframestructure.cpp src/protocols/framestructure.h
    src/protocols/binaryconfig.cpp src/protocols/binaryconfig.h
    src/protocols/protocolparser.cpp src/protocols/protocolparser.h
    src/protocols/keyvalueparser.cpp src/protocols/keyvalueparser.h
    src/protocols/protocolcatalog.cpp src/protocols/protocolcatalog.h
    src/application/protocolsession.cpp src/application/protocolsession.h)
target_include_directories(protocol_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(protocol_core PUBLIC Qt6::Core)
qt_add_resources(protocol_core protocol_templates PREFIX "/protocol-templates" BASE "${CMAKE_CURRENT_SOURCE_DIR}/templates"
    FILES templates/multi_segment.json templates/simple_fixed.json templates/simple_variable.json templates/robot_tennis.json templates/generic.json)

qt_add_library(send_core STATIC src/core/bytesink.h src/application/sendsession.cpp src/application/sendsession.h)
target_include_directories(send_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(send_core PUBLIC Qt6::Core)

qt_add_library(serial_transport STATIC
    src/sources/serial/serialsource.cpp src/sources/serial/serialsource.h)
target_link_libraries(serial_transport PUBLIC receive_core Qt6::SerialPort)

qt_add_library(numeric_data STATIC src/plotting/numericstream.cpp src/plotting/numericstream.h)
target_include_directories(numeric_data PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(numeric_data PUBLIC protocol_core Qt6::Core)

qt_add_library(recording_core STATIC src/recording/csvrecorder.cpp src/recording/csvrecorder.h)
target_include_directories(recording_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(recording_core PUBLIC Qt6::Core)

qt_add_library(uart_application STATIC
    src/application/appsettings.cpp src/application/appsettings.h
    src/application/componentlayoutstore.cpp src/application/componentlayoutstore.h
    src/presentation/serialconnectionmodel.cpp src/presentation/serialconnectionmodel.h
    src/application/applicationservices.cpp src/application/applicationservices.h)
target_link_libraries(uart_application PUBLIC serial_transport receive_core protocol_core numeric_data send_core recording_core)

qt_add_library(uart_ui STATIC src/presentation/logdocumentsettings.cpp src/presentation/logdocumentsettings.h
    src/presentation/dockwindowcontroller.cpp src/presentation/dockwindowcontroller.h
    src/presentation/sendtimer.h
    src/presentation/componentdragcontroller.cpp src/presentation/componentdragcontroller.h
    src/plotting/interactiveplot.cpp src/plotting/interactiveplot.h
    src/plotting/xyplot.cpp src/plotting/xyplot.h
    src/plotting/timeseriesplot.cpp src/plotting/timeseriesplot.h src/plotting/plotviewport.h)
target_include_directories(uart_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(uart_ui PUBLIC numeric_data Qt6::Quick)

qt_add_executable(appuart_tool main.cpp)
set(qml_files
    qml/Main.qml qml/components/Theme.qml qml/components/DockOverlay.qml qml/components/DockGuides.qml qml/components/DockPanel.qml qml/components/DockLayout.qml qml/components/SidebarFoldHandle.qml qml/components/ComponentFrame.qml qml/components/SettingsPage.qml qml/components/RecordingControls.qml
    qml/components/MainTitleBar.qml qml/components/TitleActionButton.qml qml/components/WindowResizeHandles.qml
    qml/components/TextEditingMenu.qml qml/components/AppTextField.qml qml/components/AppSpinBox.qml
    qml/components/AppButton.qml qml/components/FieldLabel.qml qml/components/HorizontalToolbar.qml
    qml/components/ConfigCombo.qml qml/components/SerialConfigPanel.qml
    qml/components/ReceivePanel.qml qml/components/RawReceivePanel.qml qml/components/ReceiveTextEdit.qml qml/components/SendPanel.qml qml/components/SendEndingCombo.qml qml/components/QuickSendCard.qml qml/components/ParameterSendEditor.qml
    qml/components/PlotParametersPopup.qml qml/components/PlotReferenceEditor.qml
    qml/components/ComponentMenu.qml qml/components/PlotPreview.qml qml/components/XYPlotCard.qml qml/components/ComponentWorkspace.qml qml/components/TimePlotCard.qml
    qml/components/ProtocolPanel.qml qml/components/ProtocolSelector.qml qml/components/BinaryCommandSelector.qml qml/components/CascadeMenuButton.qml qml/components/BinaryProtocolEditor.qml qml/components/ChecksumRuleEditor.qml qml/components/FrameStructureEditor.qml qml/components/DataFieldsEditor.qml)
set(qml_icons
    qml/icons/settings.svg qml/icons/record.svg qml/icons/stop-recording.svg
    qml/icons/edit-undo.svg qml/icons/edit-redo.svg qml/icons/edit-cut.svg
    qml/icons/edit-copy.svg qml/icons/edit-paste.svg qml/icons/edit-delete.svg qml/icons/edit-select-all.svg)
foreach(file IN LISTS qml_files qml_icons)
    string(REGEX REPLACE "^qml/" "" alias "${file}")
    set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS "${alias}")
endforeach()
set_source_files_properties(qml/components/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_resources(appuart_tool theme_directory PREFIX "/qt/qml/uart_tool" BASE "${CMAKE_CURRENT_SOURCE_DIR}/qml" FILES qml/components/qmldir)
qt_add_qml_module(appuart_tool URI uart_tool VERSION 1.0 QML_FILES ${qml_files} RESOURCES ${qml_icons})
set_target_properties(appuart_tool PROPERTIES
    MACOSX_BUNDLE TRUE WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR})
target_link_libraries(appuart_tool PRIVATE uart_application uart_ui Qt6::Quick Qt6::QuickControls2)
include(GNUInstallDirs)
install(TARGETS appuart_tool BUNDLE DESTINATION . LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
    qt_add_executable(send_tests tests/send_tests.cpp)
    target_link_libraries(send_tests PRIVATE send_core serial_transport Qt6::Test)
    add_test(NAME send COMMAND send_tests)
    qt_add_executable(receiver_tests tests/receiver_tests.cpp)
    target_link_libraries(receiver_tests PRIVATE uart_application uart_ui Qt6::Test Qt6::Quick Qt6::QuickControls2)
    target_compile_definitions(receiver_tests PRIVATE UART_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME receiver COMMAND receiver_tests)
    set_tests_properties(receiver PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 120)
    qt_add_executable(numeric_data_tests tests/numeric_data_tests.cpp)
    target_link_libraries(numeric_data_tests PRIVATE numeric_data Qt6::Test)
    add_test(NAME numeric_data COMMAND numeric_data_tests)
    qt_add_executable(receive_core_tests tests/receive_core_tests.cpp)
    target_link_libraries(receive_core_tests PRIVATE receive_core protocol_core Qt6::Test)
    add_test(NAME receive_core COMMAND receive_core_tests)
    qt_add_executable(protocol_parser_tests tests/protocol_parser_tests.cpp)
    target_link_libraries(protocol_parser_tests PRIVATE protocol_core Qt6::Test)
    add_test(NAME protocol_parser COMMAND protocol_parser_tests)
    qt_add_executable(protocol_template_tests tests/protocol_template_tests.cpp)
    target_link_libraries(protocol_template_tests PRIVATE protocol_core Qt6::Test)
    add_test(NAME protocol_templates COMMAND protocol_template_tests)
endif()
