#include "blackwire/ui/theme.hpp"

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QPalette>

namespace blackwire {

void ApplyAppTheme(QApplication& app) {
    QFont default_font("Segoe UI", 10);
    default_font.setStyleStrategy(QFont::PreferAntialias);
    app.setFont(default_font);

    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#313338"));
    palette.setColor(QPalette::WindowText, QColor("#dbdee1"));
    palette.setColor(QPalette::Base, QColor("#1e1f22"));
    palette.setColor(QPalette::AlternateBase, QColor("#2b2d31"));
    palette.setColor(QPalette::ToolTipBase, QColor("#111214"));
    palette.setColor(QPalette::ToolTipText, QColor("#dbdee1"));
    palette.setColor(QPalette::Text, QColor("#dbdee1"));
    palette.setColor(QPalette::Button, QColor("#2b2d31"));
    palette.setColor(QPalette::ButtonText, QColor("#dbdee1"));
    palette.setColor(QPalette::BrightText, QColor("#ffffff"));
    palette.setColor(QPalette::Link, QColor("#00a8fc"));
    palette.setColor(QPalette::Highlight, QColor("#5865f2"));
    palette.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    app.setPalette(palette);

    const QString qss = R"(

/* ── Global ───────────────────────────────────────────────── */
QWidget {
    background: #313338;
    color: #dbdee1;
    font-family: "Segoe UI", "Helvetica Neue", Helvetica, Arial, sans-serif;
    font-size: 14px;
}

QMainWindow, QDialog {
    background: #313338;
}

QStatusBar {
    background: #232428;
    color: #949ba4;
    border-top: 1px solid #1e1f22;
    font-size: 12px;
    padding: 2px 8px;
}

/* ── Scrollbars (Discord-thin) ────────────────────────────── */
QScrollBar:vertical {
    background: transparent;
    width: 8px;
    margin: 0;
}
QScrollBar::handle:vertical {
    background: #1a1b1e;
    border-radius: 4px;
    min-height: 40px;
}
QScrollBar::handle:vertical:hover {
    background: #27282c;
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical,
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
    background: transparent;
    height: 0;
    border: none;
}
QScrollBar:horizontal {
    background: transparent;
    height: 8px;
}
QScrollBar::handle:horizontal {
    background: #1a1b1e;
    border-radius: 4px;
    min-width: 40px;
}
QScrollBar::handle:horizontal:hover {
    background: #27282c;
}
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal,
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {
    background: transparent;
    width: 0;
    border: none;
}

/* ── Inputs ───────────────────────────────────────────────── */
QLineEdit, QPlainTextEdit, QListWidget, QComboBox {
    background: #1e1f22;
    border: none;
    border-radius: 8px;
    color: #dbdee1;
    selection-background-color: #5865f2;
    padding: 8px 12px;
    font-size: 14px;
}

QComboBox#statusCombo {
    min-width: 100px;
    background: #1e1f22;
    border-radius: 4px;
    padding: 4px 8px;
}

QLineEdit:focus, QPlainTextEdit:focus, QComboBox:focus {
    outline: none;
}

QComboBox::drop-down {
    border: none;
    width: 20px;
}
QComboBox QAbstractItemView {
    background: #2b2d31;
    border: 1px solid #1e1f22;
    border-radius: 4px;
    selection-background-color: #404249;
    color: #dbdee1;
}

/* ── Compose input (Discord chat bar) ─────────────────────── */
QPlainTextEdit#composeInput {
    background: #383a40;
    border: none;
    border-radius: 8px;
    color: #dbdee1;
    padding: 10px 16px;
    font-size: 14px;
}

/* ── Buttons ──────────────────────────────────────────────── */
QPushButton {
    border-radius: 3px;
    padding: 7px 16px;
    background: #4e5058;
    color: #ffffff;
    border: none;
    font-size: 14px;
    font-weight: 500;
}

QPushButton:hover {
    background: #6d6f78;
}

QPushButton:pressed {
    background: #43444b;
}

QPushButton:disabled {
    color: #4e5058;
    background: #2b2d31;
}

QPushButton#primaryButton {
    background: #5865f2;
    border: none;
    font-weight: 500;
}

QPushButton#primaryButton:hover {
    background: #4752c4;
}

QPushButton#primaryButton:pressed {
    background: #3c45a5;
}

QPushButton#secondaryButton {
    background: #4e5058;
}

QPushButton#secondaryButton:hover {
    background: #6d6f78;
}

QPushButton#secondaryButton:checked {
    background: #5865f2;
    color: #ffffff;
}

QPushButton#dangerButton {
    background: #da373c;
    border: none;
    color: #ffffff;
    font-weight: 500;
}

QPushButton#dangerButton:hover {
    background: #a12d31;
}

/* ── DM Sidebar (Discord channels pane) ───────────────────── */
QWidget#dmSidebar {
    background: #2b2d31;
    border-right: none;
}

QWidget#settingsSidebar {
    background: #2b2d31;
    border-right: none;
}

QListWidget#settingsTabList {
    background: transparent;
    border: none;
    padding: 4px 8px;
}

QListWidget#settingsTabList::item {
    color: #949ba4;
    border-radius: 4px;
    padding: 8px 10px;
    margin: 1px 0px;
    font-size: 14px;
}

QListWidget#settingsTabList::item:hover {
    background: #35373c;
    color: #dbdee1;
}

QListWidget#settingsTabList::item:selected {
    background: #404249;
    color: #ffffff;
    font-weight: 600;
}

QLabel#dmSidebarTitle {
    font-size: 12px;
    font-weight: 600;
    color: #949ba4;
    text-transform: uppercase;
    padding: 0 8px;
}

QLabel#contactsSidebarTitle {
    font-size: 12px;
    font-weight: 600;
    color: #949ba4;
    text-transform: uppercase;
    padding: 0 8px;
}

/* ── Identity card (Discord user panel) ───────────────────── */
QWidget#identityCard {
    background: #232428;
    border: none;
    border-radius: 0;
    border-top: 1px solid #1e1f22;
}

QWidget#contactsToggleCard {
    background: transparent;
    border: none;
    border-radius: 4px;
}

QLabel#identityLabel {
    color: #949ba4;
    font-size: 12px;
}

/* ── Chat pane ────────────────────────────────────────────── */
QWidget#chatPane {
    background: #313338;
}

/* ── Header bar ───────────────────────────────────────────── */
QWidget#chatHeaderBar {
    background: #313338;
    border-bottom: 1px solid #232428;
}

QLabel#threadTitle, QLineEdit#threadTitle {
    font-size: 16px;
    font-weight: 600;
    color: #ffffff;
    background: transparent;
    border: none;
    border-radius: 4px;
    padding: 2px 4px;
}

QLineEdit#threadTitle:focus {
    border: 1px solid #5865f2;
    background: #1e1f22;
}

/* ── Connection / status pills ────────────────────────────── */
QLabel#connectionPill {
    border-radius: 3px;
    padding: 2px 8px;
    background: #4e5058;
    color: #dbdee1;
    border: none;
    font-size: 12px;
    font-weight: 500;
}

QLabel#connectionPill[state="connected"] {
    background: #248046;
}

QLabel#connectionPill[state="warning"] {
    background: #f0b232;
    color: #1e1f22;
}

QLabel#connectionPill[state="error"] {
    background: #da373c;
}

/* ── Call status pill ─────────────────────────────────────── */
QLabel#callStatusPill {
    border-radius: 3px;
    padding: 2px 8px;
    background: #4e5058;
    color: #dbdee1;
    border: none;
    font-size: 12px;
    font-weight: 500;
}

QLabel#callStatusPill[state="ringing"] {
    background: #f0b232;
    color: #1e1f22;
}

QLabel#callStatusPill[state="active"] {
    background: #248046;
}

QLabel#callStatusPill[state="warning"] {
    background: #f0b232;
    color: #1e1f22;
}

QLabel#callStatusPill[state="error"] {
    background: #da373c;
}

/* ── Call panel ────────────────────────────────────────────── */
QWidget#callPanel {
    background: #2b2d31;
    border: 1px solid #1e1f22;
    border-radius: 8px;
}

QWidget#callPanel[state="ringing"] {
    border-color: #f0b232;
    background: #2e2b21;
}

QWidget#callPanel[state="active"] {
    border-color: #248046;
    background: #1f2d24;
}

QWidget#callPanel[state="warning"] {
    border-color: #f0b232;
}

QWidget#callPanel[state="error"] {
    border-color: #da373c;
}

QLabel#callPanelAvatar {
    background: #5865f2;
    border-radius: 28px;
    color: #ffffff;
    font-size: 20px;
    font-weight: 600;
}

QLabel#callPanelTitle {
    color: #ffffff;
    font-size: 16px;
    font-weight: 600;
}

QLabel#callPanelSubtitle {
    color: #949ba4;
    font-size: 13px;
}

QLabel#callParticipantChip {
    background: #2b2d31;
    border: 1px solid #1e1f22;
    border-radius: 12px;
    padding: 4px 10px;
    color: #dbdee1;
    font-size: 12px;
}

QLabel#callParticipantChip[self="true"] {
    background: #5865f2;
    border-color: #4752c4;
    color: #ffffff;
    font-weight: 600;
}

/* ── Chat banner ──────────────────────────────────────────── */
QLabel#chatBanner {
    border-radius: 4px;
    padding: 8px 12px;
    color: #ffffff;
    background: #4e5058;
    font-size: 14px;
}

QLabel#chatBanner[severity="info"] {
    background: #5865f2;
}

QLabel#chatBanner[severity="warning"] {
    background: #f0b232;
    color: #1e1f22;
}

QLabel#chatBanner[severity="error"] {
    background: #da373c;
}

/* ── Conversation list (Discord DM list) ──────────────────── */
QListWidget#conversationList {
    background: #2b2d31;
    border: none;
}

QListWidget#contactsList {
    background: #2b2d31;
    border: none;
}

QListWidget#conversationList::item {
    border-radius: 4px;
    padding: 0;
    margin: 1px 8px;
}

QListWidget#conversationList::item:hover {
    background: #35373c;
}

QListWidget#conversationList::item:selected {
    background: #404249;
}

QListWidget#contactsList::item {
    border-radius: 4px;
    padding: 0;
    margin: 1px 8px;
}

QListWidget#contactsList::item:hover {
    background: #35373c;
}

QListWidget#contactsList::item:selected {
    background: #404249;
}

/* ── Conversation row widget ──────────────────────────────── */
QWidget#conversationRow {
    background: transparent;
    border-radius: 4px;
}

QWidget#conversationRow[selected="true"] {
    background: #404249;
}

QLabel#conversationAvatar {
    min-width: 32px;
    max-width: 32px;
    min-height: 32px;
    max-height: 32px;
    border-radius: 16px;
    background: #5865f2;
    color: #ffffff;
    font-size: 13px;
    font-weight: 600;
}

QLabel#conversationTitle {
    color: #f2f3f5;
    font-weight: 500;
    font-size: 14px;
}

QLabel#conversationSubtitle {
    color: #949ba4;
    font-size: 12px;
}

QLabel#conversationTime {
    color: #949ba4;
    font-size: 11px;
}

QPushButton#conversationRemoveButton {
    min-width: 16px;
    max-width: 16px;
    min-height: 16px;
    max-height: 16px;
    border-radius: 3px;
    border: none;
    background: transparent;
    color: #949ba4;
    padding: 0px;
    font-size: 12px;
    font-weight: 600;
}

QPushButton#conversationRemoveButton:hover {
    background: #da373c;
    color: #ffffff;
}

/* ── Presence dot ─────────────────────────────────────────── */
QLabel#presenceDot {
    border-radius: 5px;
    min-width: 10px;
    max-width: 10px;
    min-height: 10px;
    max-height: 10px;
    background: #80848e;
}

QLabel#presenceDot[state="active"] {
    background: #23a55a;
}

QLabel#presenceDot[state="inactive"] {
    background: #f0b232;
}

QLabel#presenceDot[state="offline"] {
    background: #80848e;
}

QLabel#presenceDot[state="dnd"] {
    background: #f23f43;
}

/* ── Message list (Discord chat area) ─────────────────────── */
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

QListWidget#messageList::item:hover {
    background: #2e3035;
}

/* ── Message row (Discord flat messages) ──────────────────── */
QWidget#messageRow {
    background: transparent;
}

QLabel#messageAvatar {
    min-width: 40px;
    max-width: 40px;
    min-height: 40px;
    max-height: 40px;
    border-radius: 20px;
    background: #5865f2;
    border: none;
    color: #ffffff;
    font-size: 15px;
    font-weight: 600;
}

QLabel#messageAvatar[grouped="true"] {
    background: transparent;
    border-color: transparent;
    color: transparent;
}

/* No bubble -- flat Discord-style messages */
QWidget#messageBubble {
    border-radius: 0;
    background: transparent;
    border: none;
}

QLabel#messageSender {
    color: #f2f3f5;
    font-size: 14px;
    font-weight: 500;
}

QLabel#messageTimestamp {
    color: #949ba4;
    font-size: 12px;
}

QLabel#messageMeta {
    color: #f2f3f5;
    font-size: 14px;
    font-weight: 500;
}

QLabel#messageBody {
    color: #dbdee1;
    font-size: 14px;
}

/* ── System messages ──────────────────────────────────────── */
QWidget#systemMessageRow {
    background: transparent;
}

QLabel#systemMessageIcon {
    border-radius: 3px;
    padding: 2px 6px;
    background: #248046;
    border: none;
    color: #ffffff;
    font-size: 10px;
    font-weight: 700;
}

QLabel#systemMessageText {
    color: #949ba4;
    font-size: 13px;
}

/* ── Empty state ──────────────────────────────────────────── */
QLabel#chatEmptyState {
    color: #949ba4;
    font-size: 16px;
}

/* ── Audio meter ──────────────────────────────────────────── */
QProgressBar#audioLevelMeter {
    border: none;
    border-radius: 4px;
    background: #1e1f22;
}

QProgressBar#audioLevelMeter::chunk {
    border-radius: 4px;
    background: #23a55a;
}

/* ── Dialog labels ────────────────────────────────────────── */
QDialog QLabel {
    color: #dbdee1;
}

/* ── Splitter handle ──────────────────────────────────────── */
QSplitter::handle {
    background: #1e1f22;
}

/* ── Tooltips ─────────────────────────────────────────────── */
QToolTip {
    background: #111214;
    color: #dbdee1;
    border: 1px solid #1e1f22;
    border-radius: 4px;
    padding: 6px 10px;
    font-size: 13px;
}

/* ── Login card ───────────────────────────────────────────── */
QWidget#loginCard {
    background: #2b2d31;
    border-radius: 6px;
    border: none;
}

QLabel#loginTitle {
    color: #f2f3f5;
    font-size: 24px;
    font-weight: 600;
}

QLabel#loginSubtitle {
    color: #949ba4;
    font-size: 14px;
}

QLabel#loginFieldLabel {
    color: #b5bac1;
    font-size: 12px;
    font-weight: 700;
    text-transform: uppercase;
}
)";

    app.setStyleSheet(qss);
}

}  // namespace blackwire
