#include "blackwire/ui/chat_widget.hpp"

#include <QClipboard>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSize>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include "blackwire/models/view_models.hpp"
#include "blackwire/util/message_view.hpp"

namespace blackwire {

namespace {

QString FormatCallStatusText(const CallStateView& state) {
    const QString normalized = state.state.trimmed().toLower();
    if (normalized == "active") {
        return "In Call";
    }
    if (normalized == "incoming_ringing") {
        return "Incoming Call";
    }
    if (normalized == "outgoing_ringing") {
        return "Ringing";
    }
    if (normalized == "ending") {
        return "Ending";
    }

    const QString reason = state.reason.trimmed().toLower();
    if (reason == "busy" || reason == "peer_busy" || reason == "caller_busy") {
        return "Busy";
    }
    if (reason == "missed") {
        return "Missed";
    }
    if (reason == "declined") {
        return "Declined";
    }
    if (reason == "connection_lost" || reason == "peer_disconnected") {
        return "Disconnected";
    }
    if (reason == "ended" || reason == "ending") {
        return "Ended";
    }
    return "Idle";
}

QString FormatCallPanelSubtitle(const CallStateView& state) {
    const QString normalized = state.state.trimmed().toLower();
    if (normalized == "active") {
        return state.muted ? "Call active - You are muted" : "Call active - Open mic";
    }
    if (normalized == "incoming_ringing") {
        return "Incoming voice call";
    }
    if (normalized == "outgoing_ringing") {
        return "Calling...";
    }
    if (normalized == "ending") {
        return "Ending call...";
    }

    const QString reason = state.reason.trimmed();
    if (!reason.isEmpty()) {
        QString pretty = reason;
        pretty.replace('_', ' ');
        return QString("Last call: %1").arg(pretty);
    }
    return "Voice call idle";
}

QString CallStatusStyleState(const CallStateView& state) {
    const QString normalized = state.state.trimmed().toLower();
    if (normalized == "active") {
        return "active";
    }
    if (normalized == "incoming_ringing" || normalized == "outgoing_ringing") {
        return "ringing";
    }
    if (normalized == "ending") {
        return "warning";
    }

    const QString reason = state.reason.trimmed().toLower();
    if (reason == "busy" || reason == "peer_busy" || reason == "caller_busy") {
        return "warning";
    }
    if (reason == "missed" || reason == "connection_lost" || reason == "peer_disconnected" || reason == "audio_error") {
        return "error";
    }
    return "idle";
}

}  // namespace

ChatWidget::ChatWidget(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* split = new QSplitter(Qt::Horizontal, this);
    split->setChildrenCollapsible(false);
    split->setHandleWidth(1);

    auto* sidebar = new QWidget(split);
    sidebar->setObjectName("dmSidebar");
    sidebar->setMinimumWidth(290);
    sidebar->setMaximumWidth(360);
    auto* sidebar_layout = new QVBoxLayout(sidebar);
    sidebar_layout->setContentsMargins(14, 14, 14, 14);
    sidebar_layout->setSpacing(10);

    auto* sidebar_title = new QLabel("Direct Messages", sidebar);
    sidebar_title->setObjectName("dmSidebarTitle");
    sidebar_layout->addWidget(sidebar_title);

    auto* peer_row = new QHBoxLayout();
    peer_row->setSpacing(8);
    peer_input_ = new QLineEdit(sidebar);
    peer_input_->setPlaceholderText("peer username or username@onion");
    new_chat_button_ = new QPushButton("Open DM", sidebar);
    new_chat_button_->setObjectName("secondaryButton");
    peer_row->addWidget(peer_input_, 1);
    peer_row->addWidget(new_chat_button_);
    sidebar_layout->addLayout(peer_row);

    conversations_list_ = new QListWidget(sidebar);
    conversations_list_->setObjectName("conversationList");
    conversations_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    conversations_list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    conversations_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    conversations_list_->setSpacing(2);
    sidebar_layout->addWidget(conversations_list_, 1);

    auto* identity_card = new QWidget(sidebar);
    identity_card->setObjectName("identityCard");
    auto* identity_layout = new QVBoxLayout(identity_card);
    identity_layout->setContentsMargins(10, 10, 10, 10);
    identity_layout->setSpacing(6);

    identity_label_ = new QLabel("Your ID: -", identity_card);
    identity_label_->setObjectName("identityLabel");
    identity_label_->setWordWrap(true);
    identity_layout->addWidget(identity_label_);

    auto* identity_actions = new QHBoxLayout();
    identity_actions->setSpacing(8);
    copy_identity_button_ = new QPushButton("Copy ID", identity_card);
    copy_identity_button_->setObjectName("secondaryButton");
    copy_identity_button_->setEnabled(false);
    settings_button_ = new QPushButton("Settings", identity_card);
    settings_button_->setObjectName("secondaryButton");
    identity_actions->addWidget(copy_identity_button_);
    identity_actions->addWidget(settings_button_);
    identity_layout->addLayout(identity_actions);
    sidebar_layout->addWidget(identity_card);

    auto* content = new QWidget(split);
    content->setObjectName("chatPane");
    auto* content_layout = new QVBoxLayout(content);
    content_layout->setContentsMargins(16, 16, 16, 16);
    content_layout->setSpacing(10);

    auto* header_row = new QHBoxLayout();
    header_row->setSpacing(8);
    thread_title_label_ = new QLabel("Select a conversation", content);
    thread_title_label_->setObjectName("threadTitle");
    status_label_ = new QLabel("Disconnected", content);
    status_label_->setObjectName("connectionPill");
    status_label_->setProperty("state", "disconnected");
    call_button_ = new QPushButton("Call", content);
    call_button_->setObjectName("primaryButton");
    header_row->addWidget(thread_title_label_, 1);
    header_row->addWidget(call_button_);
    header_row->addWidget(status_label_);
    content_layout->addLayout(header_row);

    banner_label_ = new QLabel(content);
    banner_label_->setObjectName("chatBanner");
    banner_label_->setWordWrap(true);
    banner_label_->setProperty("severity", "info");
    banner_label_->setVisible(false);
    content_layout->addWidget(banner_label_);

    call_panel_ = new QWidget(content);
    call_panel_->setObjectName("callPanel");
    auto* call_panel_layout = new QHBoxLayout(call_panel_);
    call_panel_layout->setContentsMargins(12, 12, 12, 12);
    call_panel_layout->setSpacing(12);

    call_panel_avatar_ = new QLabel("#", call_panel_);
    call_panel_avatar_->setObjectName("callPanelAvatar");
    call_panel_avatar_->setAlignment(Qt::AlignCenter);
    call_panel_avatar_->setFixedSize(56, 56);
    call_panel_layout->addWidget(call_panel_avatar_, 0, Qt::AlignTop);

    auto* call_center_col = new QVBoxLayout();
    call_center_col->setSpacing(6);
    call_panel_title_ = new QLabel("Voice Call", call_panel_);
    call_panel_title_->setObjectName("callPanelTitle");
    call_panel_subtitle_ = new QLabel("Voice call idle", call_panel_);
    call_panel_subtitle_->setObjectName("callPanelSubtitle");
    call_status_label_ = new QLabel("Idle", call_panel_);
    call_status_label_->setObjectName("callStatusPill");
    call_status_label_->setProperty("state", "idle");
    call_center_col->addWidget(call_panel_title_);
    call_center_col->addWidget(call_panel_subtitle_);
    call_center_col->addWidget(call_status_label_, 0, Qt::AlignLeft);
    call_panel_layout->addLayout(call_center_col, 1);

    auto* call_controls_row = new QHBoxLayout();
    call_controls_row->setSpacing(8);
    accept_call_button_ = new QPushButton("Accept", call_panel_);
    accept_call_button_->setObjectName("primaryButton");
    decline_call_button_ = new QPushButton("Decline", call_panel_);
    decline_call_button_->setObjectName("dangerButton");
    mute_call_button_ = new QPushButton("Mute", call_panel_);
    mute_call_button_->setObjectName("secondaryButton");
    end_call_button_ = new QPushButton("End Call", call_panel_);
    end_call_button_->setObjectName("dangerButton");
    call_controls_row->addWidget(accept_call_button_);
    call_controls_row->addWidget(decline_call_button_);
    call_controls_row->addWidget(mute_call_button_);
    call_controls_row->addWidget(end_call_button_);
    call_panel_layout->addLayout(call_controls_row);

    call_panel_->setVisible(false);
    content_layout->addWidget(call_panel_);

    timeline_stack_ = new QStackedWidget(content);
    empty_state_label_ = new QLabel("Select a conversation to start chatting.", timeline_stack_);
    empty_state_label_->setObjectName("chatEmptyState");
    empty_state_label_->setAlignment(Qt::AlignCenter);

    messages_list_ = new QListWidget(timeline_stack_);
    messages_list_->setObjectName("messageList");
    messages_list_->setSelectionMode(QAbstractItemView::NoSelection);
    messages_list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    messages_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    messages_list_->setSpacing(4);

    timeline_stack_->addWidget(empty_state_label_);
    timeline_stack_->addWidget(messages_list_);
    timeline_stack_->setCurrentWidget(empty_state_label_);
    content_layout->addWidget(timeline_stack_, 1);

    auto* compose_row = new QHBoxLayout();
    compose_row->setSpacing(10);
    compose_input_ = new QPlainTextEdit(content);
    compose_input_->setObjectName("composeInput");
    compose_input_->setPlaceholderText("Message #dm");
    compose_input_->setMaximumBlockCount(200);
    compose_input_->setFixedHeight(96);
    compose_input_->installEventFilter(this);

    send_button_ = new QPushButton("Send", content);
    send_button_->setObjectName("primaryButton");
    send_button_->setEnabled(false);

    compose_row->addWidget(compose_input_, 1);
    compose_row->addWidget(send_button_);
    content_layout->addLayout(compose_row);

    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    root->addWidget(split, 1);

    banner_timer_ = new QTimer(this);
    banner_timer_->setSingleShot(true);
    connect(banner_timer_, &QTimer::timeout, this, [this]() {
        banner_label_->setVisible(false);
    });

    auto* focus_peer_shortcut = new QShortcut(QKeySequence("Ctrl+K"), this);
    connect(focus_peer_shortcut, &QShortcut::activated, this, [this]() {
        peer_input_->setFocus();
        peer_input_->selectAll();
    });

    connect(new_chat_button_, &QPushButton::clicked, this, &ChatWidget::NewConversationRequested);
    connect(send_button_, &QPushButton::clicked, this, &ChatWidget::SendMessageRequested);
    connect(settings_button_, &QPushButton::clicked, this, &ChatWidget::SettingsRequested);
    connect(call_button_, &QPushButton::clicked, this, &ChatWidget::StartVoiceCallRequested);
    connect(accept_call_button_, &QPushButton::clicked, this, &ChatWidget::AcceptVoiceCallRequested);
    connect(decline_call_button_, &QPushButton::clicked, this, &ChatWidget::RejectVoiceCallRequested);
    connect(end_call_button_, &QPushButton::clicked, this, &ChatWidget::EndVoiceCallRequested);
    connect(mute_call_button_, &QPushButton::clicked, this, [this]() {
        emit CallMuteToggled(!call_state_.muted);
    });

    connect(copy_identity_button_, &QPushButton::clicked, this, [this]() {
        if (identity_value_.isEmpty()) {
            return;
        }
        auto* clipboard = QGuiApplication::clipboard();
        if (clipboard != nullptr) {
            clipboard->setText(identity_value_);
        }
    });

    connect(compose_input_, &QPlainTextEdit::textChanged, this, [this]() {
        SetSendEnabled(!ComposeText().trimmed().isEmpty());
    });

    connect(conversations_list_, &QListWidget::itemSelectionChanged, this, [this]() {
        RefreshConversationSelectionStyles();
        UpdateThreadHeader();
        UpdateCallControls();

        const QString id = SelectedConversation();
        if (id.isEmpty()) {
            SetTimelineHasMessages(false);
            return;
        }

        messages_list_->clear();
        SetTimelineHasMessages(false);
        emit ConversationSelected(id);
    });

    UpdateCallControls();
}

QString ChatWidget::PeerUsername() const {
    return peer_input_->text().trimmed();
}

QString ChatWidget::ComposeText() const {
    return compose_input_->toPlainText();
}

QString ChatWidget::SelectedConversation() const {
    const auto* item = conversations_list_->currentItem();
    if (item == nullptr) {
        return {};
    }
    return item->data(Qt::UserRole).toString();
}

void ChatWidget::SetSelectedConversation(const QString& conversation_id) {
    QSignalBlocker signal_blocker(conversations_list_);
    if (conversation_id.isEmpty()) {
        conversations_list_->setCurrentItem(nullptr);
        RefreshConversationSelectionStyles();
        UpdateThreadHeader();
        UpdateCallControls();
        return;
    }

    for (int index = 0; index < conversations_list_->count(); ++index) {
        auto* item = conversations_list_->item(index);
        if (item->data(Qt::UserRole).toString() != conversation_id) {
            continue;
        }
        conversations_list_->setCurrentItem(item);
        break;
    }

    RefreshConversationSelectionStyles();
    UpdateThreadHeader();
    UpdateCallControls();
}

QWidget* ChatWidget::CreateConversationItemWidget(const ConversationListItemView& item, bool selected) const {
    auto* row = new QWidget();
    row->setObjectName("conversationRow");
    row->setProperty("selected", selected);

    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(8);

    auto* text_col = new QVBoxLayout();
    text_col->setSpacing(2);

    auto* title = new QLabel(item.title, row);
    title->setObjectName("conversationTitle");
    title->setWordWrap(false);

    auto* subtitle = new QLabel(item.subtitle, row);
    subtitle->setObjectName("conversationSubtitle");
    subtitle->setWordWrap(false);

    text_col->addWidget(title);
    text_col->addWidget(subtitle);

    auto* time = new QLabel(FormatThreadTimestamp(item.last_activity_at), row);
    time->setObjectName("conversationTime");
    time->setAlignment(Qt::AlignTop | Qt::AlignRight);

    layout->addLayout(text_col, 1);
    layout->addWidget(time);

    return row;
}

QWidget* ChatWidget::CreateThreadMessageWidget(const ThreadMessageView& message) const {
    auto* row = new QWidget();
    row->setObjectName("messageRow");
    row->setProperty("outgoing", message.outgoing);

    auto* row_layout = new QHBoxLayout(row);
    row_layout->setContentsMargins(10, message.grouped_with_previous ? 2 : 8, 10, 2);
    row_layout->setSpacing(0);

    auto* bubble = new QWidget(row);
    bubble->setObjectName("messageBubble");
    bubble->setProperty("outgoing", message.outgoing);

    auto* bubble_layout = new QVBoxLayout(bubble);
    bubble_layout->setContentsMargins(10, 8, 10, 8);
    bubble_layout->setSpacing(4);

    if (!message.grouped_with_previous) {
        auto* meta = new QLabel(QString("%1  %2").arg(message.sender_label, message.created_at_display), bubble);
        meta->setObjectName("messageMeta");
        bubble_layout->addWidget(meta);
    }

    auto* body = new QLabel(message.body, bubble);
    body->setObjectName("messageBody");
    body->setWordWrap(true);
    body->setTextInteractionFlags(Qt::TextSelectableByMouse);
    bubble_layout->addWidget(body);

    if (message.outgoing) {
        row_layout->addStretch(1);
        row_layout->addWidget(bubble, 0);
    } else {
        row_layout->addWidget(bubble, 0);
        row_layout->addStretch(1);
    }

    return row;
}

void ChatWidget::SetConversationList(const std::vector<ConversationListItemView>& conversations) {
    const QString selected_id = SelectedConversation();
    QSignalBlocker signal_blocker(conversations_list_);

    conversations_list_->clear();
    for (const auto& item : conversations) {
        auto* list_item = new QListWidgetItem(conversations_list_);
        list_item->setData(Qt::UserRole, item.id);
        list_item->setData(Qt::UserRole + 1, item.title);
        list_item->setData(Qt::UserRole + 2, item.subtitle);
        list_item->setSizeHint(QSize(200, 64));

        auto* row = CreateConversationItemWidget(item, item.id == selected_id);
        conversations_list_->setItemWidget(list_item, row);

        if (!selected_id.isEmpty() && item.id == selected_id) {
            conversations_list_->setCurrentItem(list_item);
        }
    }

    RefreshConversationSelectionStyles();
    UpdateThreadHeader();
    UpdateCallControls();
    if (SelectedConversation().isEmpty()) {
        messages_list_->clear();
        SetTimelineHasMessages(false);
    }
}

void ChatWidget::SetThreadMessages(const std::vector<ThreadMessageView>& messages) {
    messages_list_->clear();
    for (const auto& message : messages) {
        auto* item = new QListWidgetItem(messages_list_);
        auto* row = CreateThreadMessageWidget(message);
        item->setSizeHint(row->sizeHint());
        messages_list_->setItemWidget(item, row);
    }

    SetTimelineHasMessages(!messages.empty());
    if (!messages.empty()) {
        ScrollTimelineToBottom();
    }
}

void ChatWidget::AppendThreadMessage(const ThreadMessageView& message) {
    const bool near_bottom = IsTimelineNearBottom();

    auto* item = new QListWidgetItem(messages_list_);
    auto* row = CreateThreadMessageWidget(message);
    item->setSizeHint(row->sizeHint());
    messages_list_->setItemWidget(item, row);

    SetTimelineHasMessages(messages_list_->count() > 0);
    if (near_bottom) {
        ScrollTimelineToBottom();
    }
}

bool ChatWidget::IsTimelineNearBottom() const {
    auto* scroll = messages_list_->verticalScrollBar();
    if (scroll == nullptr) {
        return true;
    }
    return scroll->value() >= (scroll->maximum() - 8);
}

void ChatWidget::ScrollTimelineToBottom() {
    messages_list_->scrollToBottom();
}

void ChatWidget::SetTimelineHasMessages(bool has_messages) {
    if (!has_messages) {
        timeline_stack_->setCurrentWidget(empty_state_label_);
        return;
    }
    timeline_stack_->setCurrentWidget(messages_list_);
}

void ChatWidget::UpdateThreadHeader() {
    const auto* selected = conversations_list_->currentItem();
    if (selected == nullptr) {
        thread_title_label_->setText("Select a conversation");
        empty_state_label_->setText("Select a conversation to start chatting.");
        return;
    }

    const QString title = selected->data(Qt::UserRole + 1).toString();
    thread_title_label_->setText(title.isEmpty() ? "Direct message" : title);
    if (messages_list_->count() == 0) {
        empty_state_label_->setText("No messages yet. Send the first encrypted message.");
    }
}

void ChatWidget::RefreshConversationSelectionStyles() {
    for (int index = 0; index < conversations_list_->count(); ++index) {
        auto* item = conversations_list_->item(index);
        auto* row = conversations_list_->itemWidget(item);
        if (row == nullptr) {
            continue;
        }
        const bool selected = conversations_list_->currentRow() == index;
        row->setProperty("selected", selected);
        row->style()->unpolish(row);
        row->style()->polish(row);
    }
}

void ChatWidget::UpdateCallControls() {
    const QString state = call_state_.state.trimmed().toLower();
    const bool has_selection = !SelectedConversation().isEmpty();
    const bool panel_visible =
        state == "incoming_ringing" || state == "outgoing_ringing" || state == "active" || state == "ending";

    call_status_label_->setText(FormatCallStatusText(call_state_));
    call_status_label_->setProperty("state", CallStatusStyleState(call_state_));
    call_status_label_->style()->unpolish(call_status_label_);
    call_status_label_->style()->polish(call_status_label_);

    call_panel_->setVisible(panel_visible);
    call_button_->setVisible(state == "idle" && !panel_visible);
    call_button_->setEnabled(has_selection);
    accept_call_button_->setVisible(state == "incoming_ringing");
    decline_call_button_->setVisible(state == "incoming_ringing");
    mute_call_button_->setVisible(state == "active");
    mute_call_button_->setText(call_state_.muted ? "Unmute" : "Mute");
    end_call_button_->setVisible(state == "active" || state == "outgoing_ringing" || state == "ending");
    end_call_button_->setEnabled(state != "ending");

    QString peer_title = thread_title_label_->text().trimmed();
    if (peer_title.isEmpty()) {
        peer_title = "Direct Message";
    }
    call_panel_title_->setText(QString("Voice with %1").arg(peer_title));
    call_panel_subtitle_->setText(FormatCallPanelSubtitle(call_state_));

    QChar initial = peer_title.isEmpty() ? QChar('#') : peer_title.at(0).toUpper();
    call_panel_avatar_->setText(QString(initial));

    call_panel_->setProperty("state", CallStatusStyleState(call_state_));
    call_panel_->style()->unpolish(call_panel_);
    call_panel_->style()->polish(call_panel_);
}

void ChatWidget::SetConnectionStatus(const QString& status) {
    status_label_->setText(status);

    QString state = "disconnected";
    if (status.contains("disconnected", Qt::CaseInsensitive)) {
        state = "disconnected";
    } else if (status.contains("connected", Qt::CaseInsensitive)) {
        state = "connected";
    } else if (status.contains("reconnecting", Qt::CaseInsensitive)) {
        state = "warning";
    } else if (status.contains("auth expired", Qt::CaseInsensitive)) {
        state = "error";
    }

    status_label_->setProperty("state", state);
    status_label_->style()->unpolish(status_label_);
    status_label_->style()->polish(status_label_);
}

void ChatWidget::SetCallState(const CallStateView& state) {
    call_state_ = state;
    UpdateCallControls();
}

void ChatWidget::SetIdentity(const QString& user_address) {
    if (user_address.trimmed().isEmpty()) {
        identity_value_.clear();
        identity_label_->setText("Your ID: -");
        copy_identity_button_->setEnabled(false);
        return;
    }

    identity_value_ = user_address.trimmed();
    identity_label_->setText(QString("Your ID: %1").arg(identity_value_));
    copy_identity_button_->setEnabled(true);
}

void ChatWidget::ShowBanner(const QString& text, const QString& severity) {
    QString normalized = severity.trimmed().toLower();
    if (normalized != "error" && normalized != "warning" && normalized != "info") {
        normalized = "info";
    }

    banner_label_->setProperty("severity", normalized);
    banner_label_->style()->unpolish(banner_label_);
    banner_label_->style()->polish(banner_label_);
    banner_label_->setText(text);
    banner_label_->setVisible(true);
    banner_timer_->start(4500);
}

void ChatWidget::ClearCompose() {
    compose_input_->clear();
    SetSendEnabled(false);
}

void ChatWidget::SetSendEnabled(bool enabled) {
    send_button_->setEnabled(enabled);
}

void ChatWidget::SetSettingsVisible(bool visible) {
    settings_button_->setVisible(visible);
}

bool ChatWidget::eventFilter(QObject* watched, QEvent* event) {
    if (watched == compose_input_ && event->type() == QEvent::KeyPress) {
        auto* key_event = static_cast<QKeyEvent*>(event);
        if ((key_event->key() == Qt::Key_Return || key_event->key() == Qt::Key_Enter) &&
            (key_event->modifiers() & Qt::ShiftModifier) == 0) {
            if (send_button_->isEnabled()) {
                emit SendMessageRequested();
            }
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

}  // namespace blackwire
