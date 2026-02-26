#include "blackwire/ui/theme.hpp"

#include <QApplication>
#include <QColor>
#include <QPalette>

namespace blackwire {

void ApplyAppTheme(QApplication& app) {
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#1e1f22"));
    palette.setColor(QPalette::WindowText, QColor("#f2f3f5"));
    palette.setColor(QPalette::Base, QColor("#2b2d31"));
    palette.setColor(QPalette::AlternateBase, QColor("#313338"));
    palette.setColor(QPalette::ToolTipBase, QColor("#313338"));
    palette.setColor(QPalette::ToolTipText, QColor("#f2f3f5"));
    palette.setColor(QPalette::Text, QColor("#f2f3f5"));
    palette.setColor(QPalette::Button, QColor("#313338"));
    palette.setColor(QPalette::ButtonText, QColor("#f2f3f5"));
    palette.setColor(QPalette::BrightText, QColor("#ffffff"));
    palette.setColor(QPalette::Link, QColor("#5865f2"));
    palette.setColor(QPalette::Highlight, QColor("#5865f2"));
    palette.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    app.setPalette(palette);

    const QString qss = R"(
QWidget {
    background: #2b2d31;
    color: #f2f3f5;
    font-size: 13px;
}

QMainWindow, QDialog {
    background: #1e1f22;
}

QStatusBar {
    background: #1e1f22;
    color: #b5bac1;
    border-top: 1px solid #24262a;
}

QLineEdit, QPlainTextEdit, QListWidget, QComboBox {
    background: #1e1f22;
    border: 1px solid #1b1c20;
    border-radius: 8px;
    color: #f2f3f5;
    selection-background-color: #5865f2;
    padding: 6px 8px;
}

QComboBox#statusCombo {
    min-width: 96px;
}

QLineEdit:focus, QPlainTextEdit:focus, QComboBox:focus {
    border-color: #5865f2;
}

QComboBox::drop-down {
    border: none;
    width: 20px;
}

QPushButton {
    border-radius: 8px;
    padding: 6px 12px;
    background: #3f4248;
    color: #f2f3f5;
    border: 1px solid #24262a;
}

QPushButton:hover {
    background: #4a4d54;
}

QPushButton:pressed {
    background: #35383e;
}

QPushButton:disabled {
    color: #7c8088;
    background: #2c2f34;
}

QPushButton#primaryButton {
    background: #5865f2;
    border: 1px solid #4a57d8;
    font-weight: 600;
}

QPushButton#primaryButton:hover {
    background: #4e5ada;
}

QPushButton#primaryButton:pressed {
    background: #434dc0;
}

QPushButton#secondaryButton {
    background: #3a3d44;
}

QPushButton#secondaryButton:checked {
    background: #4e5ada;
    border: 1px solid #4a57d8;
    color: #ffffff;
}

QPushButton#dangerButton {
    background: #da373c;
    border: 1px solid #b32f33;
    color: #ffffff;
    font-weight: 600;
}

QPushButton#dangerButton:hover {
    background: #c63237;
}

QWidget#dmSidebar {
    background: #2b2d31;
    border-right: 1px solid #1b1c20;
}

QWidget#settingsSidebar {
    background: #232428;
    border-right: 1px solid #1b1c20;
}

QListWidget#settingsTabList {
    background: transparent;
    border: none;
    padding: 2px;
}

QListWidget#settingsTabList::item {
    color: #b5bac1;
    border-radius: 8px;
    padding: 9px 10px;
    margin: 1px 0px;
}

QListWidget#settingsTabList::item:hover {
    background: #2e3035;
    color: #eef0f3;
}

QListWidget#settingsTabList::item:selected {
    background: #3a3d45;
    color: #ffffff;
    font-weight: 600;
}

QLabel#dmSidebarTitle {
    font-size: 14px;
    font-weight: 700;
    color: #ffffff;
}

QLabel#contactsSidebarTitle {
    font-size: 13px;
    font-weight: 700;
    color: #d6d9de;
}

QWidget#identityCard {
    background: #232428;
    border: 1px solid #1b1c20;
    border-radius: 10px;
}

QWidget#contactsToggleCard {
    background: #232428;
    border: 1px solid #1b1c20;
    border-radius: 10px;
}

QLabel#identityLabel {
    color: #d6d9de;
}

QWidget#chatPane {
    background: #313338;
}

QLabel#threadTitle, QLineEdit#threadTitle {
    font-size: 15px;
    font-weight: 700;
    color: #ffffff;
    background: transparent;
    border: 1px solid transparent;
    border-radius: 6px;
    padding: 2px 6px;
}

QLineEdit#threadTitle:focus {
    border-color: #5865f2;
    background: #232428;
}

QLabel#connectionPill {
    border-radius: 10px;
    padding: 2px 8px;
    background: #3f4248;
    color: #d6d9de;
    border: 1px solid #2b2d31;
}

QLabel#connectionPill[state=\"connected\"] {
    background: #2d7d46;
    border-color: #236438;
}

QLabel#connectionPill[state=\"warning\"] {
    background: #9a6a1b;
    border-color: #7e5615;
}

QLabel#connectionPill[state=\"error\"] {
    background: #9e2c31;
    border-color: #7f2428;
}

QLabel#callStatusPill {
    border-radius: 10px;
    padding: 2px 8px;
    background: #3f4248;
    color: #d6d9de;
    border: 1px solid #2b2d31;
}

QLabel#callStatusPill[state=\"ringing\"] {
    background: #9a6a1b;
    border-color: #7e5615;
}

QLabel#callStatusPill[state=\"active\"] {
    background: #2d7d46;
    border-color: #236438;
}

QLabel#callStatusPill[state=\"warning\"] {
    background: #9a6a1b;
    border-color: #7e5615;
}

QLabel#callStatusPill[state=\"error\"] {
    background: #9e2c31;
    border-color: #7f2428;
}

QWidget#callPanel {
    background: #23262c;
    border: 1px solid #1b1c20;
    border-radius: 12px;
}

QWidget#callPanel[state=\"ringing\"] {
    border-color: #9a6a1b;
    background: #2d2921;
}

QWidget#callPanel[state=\"active\"] {
    border-color: #236438;
    background: #1f2d24;
}

QWidget#callPanel[state=\"warning\"] {
    border-color: #9a6a1b;
}

QWidget#callPanel[state=\"error\"] {
    border-color: #7f2428;
}

QLabel#callPanelAvatar {
    background: #5865f2;
    border-radius: 28px;
    color: #ffffff;
    font-size: 22px;
    font-weight: 700;
}

QLabel#callPanelTitle {
    color: #ffffff;
    font-size: 16px;
    font-weight: 700;
}

QLabel#callPanelSubtitle {
    color: #c6cbd2;
}

QLabel#callParticipantChip {
    background: #31353d;
    border: 1px solid #1b1c20;
    border-radius: 10px;
    padding: 3px 8px;
    color: #d6d9de;
    font-size: 12px;
}

QLabel#callParticipantChip[self=\"true\"] {
    background: #4250c7;
    border-color: #4a57d8;
    color: #ffffff;
    font-weight: 600;
}

QLabel#chatBanner {
    border-radius: 8px;
    padding: 8px 10px;
    color: #ffffff;
    background: #3f4248;
}

QLabel#chatBanner[severity=\"info\"] {
    background: #3756d9;
}

QLabel#chatBanner[severity=\"warning\"] {
    background: #9a6a1b;
}

QLabel#chatBanner[severity=\"error\"] {
    background: #da373c;
}

QListWidget#conversationList {
    background: #2b2d31;
    border: 1px solid #1b1c20;
}

QListWidget#contactsList {
    background: #2b2d31;
    border: 1px solid #1b1c20;
}

QListWidget#conversationList::item {
    border-radius: 8px;
}

QListWidget#conversationList::item:selected {
    background: #404249;
}

QListWidget#contactsList::item {
    border-radius: 8px;
}

QListWidget#contactsList::item:selected {
    background: #404249;
}

QWidget#conversationRow {
    background: transparent;
    border-radius: 8px;
}

QWidget#conversationRow[selected=\"true\"] {
    background: #404249;
}

QLabel#conversationTitle {
    color: #f2f3f5;
    font-weight: 600;
}

QLabel#conversationSubtitle {
    color: #b5bac1;
}

QLabel#conversationTime {
    color: #9aa0aa;
    font-size: 11px;
}

QPushButton#conversationRemoveButton {
    min-width: 18px;
    max-width: 18px;
    min-height: 18px;
    max-height: 18px;
    border-radius: 9px;
    border: 1px solid #2e3138;
    background: #2c2f35;
    color: #cfd3da;
    padding: 0px;
    font-weight: 600;
}

QPushButton#conversationRemoveButton:hover {
    background: #c63237;
    border-color: #b32f33;
    color: #ffffff;
}

QLabel#presenceDot {
    border-radius: 5px;
    min-width: 10px;
    max-width: 10px;
    min-height: 10px;
    max-height: 10px;
    background: #6a6f78;
}

QLabel#presenceDot[state=\"active\"] {
    background: #2d7d46;
}

QLabel#presenceDot[state=\"inactive\"] {
    background: #d4a63c;
}

QLabel#presenceDot[state=\"offline\"] {
    background: #6a6f78;
}

QLabel#presenceDot[state=\"dnd\"] {
    background: #9e2c31;
}

QListWidget#messageList {
    background: #313338;
    border: none;
}

QListWidget#messageList::item {
    background: transparent;
    border: none;
    margin: 0px;
    padding: 0px;
}

QWidget#messageRow {
    background: transparent;
}

QWidget#messageBubble {
    border-radius: 10px;
    background: #2b2d31;
    border: 1px solid #232428;
}

QWidget#messageBubble[outgoing=\"true\"] {
    background: #5865f2;
    border-color: #4f5bda;
}

QLabel#messageMeta {
    color: #b8bec7;
    font-size: 11px;
    font-weight: 600;
}

QLabel#messageBody {
    color: #f2f3f5;
}

QLabel#chatEmptyState {
    color: #b5bac1;
    font-size: 14px;
}

QProgressBar#audioLevelMeter {
    border: 1px solid #1b1c20;
    border-radius: 6px;
    background: #1e1f22;
}

QProgressBar#audioLevelMeter::chunk {
    border-radius: 6px;
    background: #3ba55d;
}

QDialog QLabel {
    color: #d6d9de;
}
)";

    app.setStyleSheet(qss);
}

}  // namespace blackwire
