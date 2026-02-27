#include "blackwire/ui/chat_widget.hpp"

#include <algorithm>
#include <cstring>

#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
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
#include <Qt>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include "blackwire/models/view_models.hpp"
#include "blackwire/util/message_view.hpp"

namespace blackwire {

namespace {

const char* kFileMessagePrefix = "bwfile://v1:";
constexpr int kRoleConversationId = Qt::UserRole;
constexpr int kRoleTitle = Qt::UserRole + 1;
constexpr int kRoleSubtitle = Qt::UserRole + 2;
constexpr int kRoleStatus = Qt::UserRole + 3;
constexpr int kRoleConversationType = Qt::UserRole + 4;
constexpr int kRoleCanManageMembers = Qt::UserRole + 5;
constexpr int kRolePeerAddress = Qt::UserRole + 6;
constexpr int kRoleGroupName = Qt::UserRole + 7;
constexpr int kRoleMemberCount = Qt::UserRole + 8;

QString NormalizePresenceStatus(const QString& value) {
    const QString normalized = value.trimmed().toLower();
    if (normalized == "active" || normalized == "inactive" || normalized == "offline" || normalized == "dnd") {
        return normalized;
    }
    return "offline";
}

QString PresenceColorForStatus(const QString& value) {
    const QString normalized = NormalizePresenceStatus(value);
    if (normalized == "active") {
        return "#2d7d46";  // green
    }
    if (normalized == "inactive") {
        return "#d4a63c";  // yellow
    }
    if (normalized == "dnd") {
        return "#9e2c31";  // red
    }
    return "#6a6f78";  // offline/default gray
}

QString HumanFileSize(qint64 size_bytes) {
    if (size_bytes >= 1024 * 1024) {
        return QString("%1 MB").arg(QString::number(static_cast<double>(size_bytes) / (1024.0 * 1024.0), 'f', 1));
    }
    if (size_bytes >= 1024) {
        return QString("%1 KB").arg(QString::number(static_cast<double>(size_bytes) / 1024.0, 'f', 1));
    }
    return QString("%1 bytes").arg(size_bytes);
}

bool DecodeFileMessageBody(
    const QString& body,
    QString* file_name,
    QByteArray* file_bytes,
    qint64* file_size_bytes) {
    if (!body.startsWith(kFileMessagePrefix, Qt::CaseInsensitive)) {
        return false;
    }
    const QString encoded = body.mid(static_cast<int>(strlen(kFileMessagePrefix))).trimmed();
    if (encoded.isEmpty()) {
        return false;
    }
    const QByteArray decoded_json = QByteArray::fromBase64(encoded.toUtf8());
    if (decoded_json.isEmpty()) {
        return false;
    }

    QJsonParseError parse_error{};
    const QJsonDocument doc = QJsonDocument::fromJson(decoded_json, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }
    const QJsonObject obj = doc.object();
    const QString candidate_name = obj.value("name").toString().trimmed();
    const QString data_b64 = obj.value("data_b64").toString();
    if (candidate_name.isEmpty() || data_b64.isEmpty()) {
        return false;
    }
    const QByteArray bytes = QByteArray::fromBase64(data_b64.toUtf8());
    if (bytes.isEmpty()) {
        return false;
    }

    if (file_name != nullptr) {
        *file_name = candidate_name;
    }
    if (file_bytes != nullptr) {
        *file_bytes = bytes;
    }
    if (file_size_bytes != nullptr) {
        *file_size_bytes = static_cast<qint64>(obj.value("size").toDouble(static_cast<double>(bytes.size())));
    }
    return true;
}

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

    auto* sidebar_title = new QLabel("Messages", sidebar);
    sidebar_title->setObjectName("dmSidebarTitle");
    sidebar_layout->addWidget(sidebar_title);

    auto* contacts_toggle_card = new QWidget(sidebar);
    contacts_toggle_card->setObjectName("contactsToggleCard");
    auto* contacts_toggle_layout = new QHBoxLayout(contacts_toggle_card);
    contacts_toggle_layout->setContentsMargins(10, 8, 10, 8);
    contacts_toggle_layout->setSpacing(0);
    contacts_button_ = new QPushButton("Contacts", contacts_toggle_card);
    contacts_button_->setObjectName("secondaryButton");
    contacts_button_->setCheckable(true);
    contacts_toggle_layout->addWidget(contacts_button_);
    sidebar_layout->addWidget(contacts_toggle_card);

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
    thread_title_label_ = new QLineEdit("Select a conversation", content);
    thread_title_label_->setObjectName("threadTitle");
    thread_title_label_->setReadOnly(true);
    thread_title_label_->setFrame(false);
    thread_title_label_->setFocusPolicy(Qt::NoFocus);
    thread_title_label_->setCursor(Qt::ArrowCursor);
    call_button_ = new QPushButton("Call", content);
    call_button_->setObjectName("primaryButton");
    group_button_ = new QPushButton("Group", content);
    group_button_->setObjectName("secondaryButton");
    invite_button_ = new QPushButton("Invite", content);
    invite_button_->setObjectName("secondaryButton");
    presence_indicator_ = new QLabel(content);
    presence_indicator_->setObjectName("presenceDot");
    presence_indicator_->setFixedSize(10, 10);
    presence_indicator_->setProperty("state", "offline");
    presence_combo_ = new QComboBox(content);
    presence_combo_->setObjectName("statusCombo");
    presence_combo_->addItem("Active", "active");
    presence_combo_->addItem("Inactive", "inactive");
    presence_combo_->addItem("Offline", "offline");
    presence_combo_->addItem("DND", "dnd");
    status_label_ = new QLabel("Disconnected", content);
    status_label_->setObjectName("connectionPill");
    status_label_->setProperty("state", "disconnected");
    header_row->addWidget(thread_title_label_, 1);
    header_row->addWidget(call_button_);
    header_row->addWidget(group_button_);
    header_row->addWidget(invite_button_);
    header_row->addWidget(presence_indicator_);
    header_row->addWidget(presence_combo_);
    header_row->addWidget(status_label_);
    content_layout->addLayout(header_row);

    banner_label_ = new QLabel(content);
    banner_label_->setObjectName("chatBanner");
    banner_label_->setWordWrap(true);
    banner_label_->setProperty("severity", "info");
    banner_label_->setVisible(false);
    content_layout->addWidget(banner_label_);
    content_stack_ = new QStackedWidget(content);

    chat_panel_ = new QWidget(content_stack_);
    auto* chat_layout = new QVBoxLayout(chat_panel_);
    chat_layout->setContentsMargins(0, 0, 0, 0);
    chat_layout->setSpacing(10);

    call_panel_ = new QWidget(chat_panel_);
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
    call_participants_panel_ = new QWidget(call_panel_);
    call_participants_panel_->setObjectName("callParticipantsPanel");
    call_participants_layout_ = new QHBoxLayout(call_participants_panel_);
    call_participants_layout_->setContentsMargins(0, 0, 0, 0);
    call_participants_layout_->setSpacing(6);
    call_center_col->addWidget(call_panel_title_);
    call_center_col->addWidget(call_panel_subtitle_);
    call_center_col->addWidget(call_status_label_, 0, Qt::AlignLeft);
    call_center_col->addWidget(call_participants_panel_, 0, Qt::AlignLeft);
    call_panel_layout->addLayout(call_center_col, 1);
    call_participants_panel_->setVisible(false);

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
    chat_layout->addWidget(call_panel_);

    timeline_stack_ = new QStackedWidget(chat_panel_);
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
    chat_layout->addWidget(timeline_stack_, 1);

    auto* compose_row = new QHBoxLayout();
    compose_row->setSpacing(10);
    compose_input_ = new QPlainTextEdit(chat_panel_);
    compose_input_->setObjectName("composeInput");
    compose_input_->setPlaceholderText("Message #dm");
    compose_input_->setMaximumBlockCount(200);
    compose_input_->setFixedHeight(96);
    compose_input_->installEventFilter(this);

    attach_button_ = new QPushButton("Attach", chat_panel_);
    attach_button_->setObjectName("secondaryButton");
    send_button_ = new QPushButton("Send", chat_panel_);
    send_button_->setObjectName("primaryButton");
    send_button_->setEnabled(false);

    compose_row->addWidget(compose_input_, 1);
    compose_row->addWidget(attach_button_);
    compose_row->addWidget(send_button_);
    chat_layout->addLayout(compose_row);

    contacts_panel_ = new QWidget(content_stack_);
    auto* contacts_layout = new QVBoxLayout(contacts_panel_);
    contacts_layout->setContentsMargins(0, 0, 0, 0);
    contacts_layout->setSpacing(8);

    auto* contacts_title = new QLabel("Contacts", contacts_panel_);
    contacts_title->setObjectName("contactsSidebarTitle");
    contacts_layout->addWidget(contacts_title);

    contacts_panel_list_ = new QListWidget(contacts_panel_);
    contacts_panel_list_->setObjectName("contactsList");
    contacts_panel_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    contacts_panel_list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    contacts_panel_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    contacts_panel_list_->setSpacing(2);
    contacts_layout->addWidget(contacts_panel_list_, 1);

    content_stack_->addWidget(chat_panel_);
    content_stack_->addWidget(contacts_panel_);
    content_layout->addWidget(content_stack_, 1);

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
    connect(new_chat_button_, &QPushButton::clicked, this, [this]() {
        SetContactsMode(false);
    });
    connect(send_button_, &QPushButton::clicked, this, &ChatWidget::SendMessageRequested);
    connect(attach_button_, &QPushButton::clicked, this, [this]() {
        const QString file_path = QFileDialog::getOpenFileName(this, "Select file to share");
        if (!file_path.trimmed().isEmpty()) {
            emit SendFileRequested(file_path.trimmed());
        }
    });
    connect(settings_button_, &QPushButton::clicked, this, &ChatWidget::SettingsRequested);
    connect(call_button_, &QPushButton::clicked, this, &ChatWidget::StartVoiceCallRequested);
    connect(group_button_, &QPushButton::clicked, this, &ChatWidget::CreateGroupFromDmRequested);
    connect(invite_button_, &QPushButton::clicked, this, &ChatWidget::GroupInviteDialogRequested);
    connect(thread_title_label_, &QLineEdit::returnPressed, this, [this]() {
        if (contacts_mode_active_) {
            return;
        }
        const auto* selected = conversations_list_->currentItem();
        if (selected == nullptr) {
            return;
        }
        const QString conversation_type = selected->data(kRoleConversationType).toString().trimmed().toLower();
        const bool can_manage_members = selected->data(kRoleCanManageMembers).toBool();
        if (conversation_type != "group" || !can_manage_members) {
            return;
        }

        const QString current_name = selected->data(kRoleGroupName).toString().trimmed();
        const QString typed_name = thread_title_label_->text().trimmed();
        if (typed_name.isEmpty()) {
            thread_title_label_->setText(current_name.isEmpty() ? selected->data(kRoleTitle).toString() : current_name);
            return;
        }
        if (!current_name.isEmpty() && typed_name == current_name) {
            return;
        }
        emit GroupRenameRequested(typed_name);
    });
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

    connect(presence_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index < 0) {
            return;
        }
        const QString status = NormalizePresenceStatus(presence_combo_->itemData(index).toString());
        UpdatePresenceIndicator(status);
        emit UserStatusChanged(status);
    });

    connect(contacts_button_, &QPushButton::clicked, this, [this]() {
        SetContactsMode(!contacts_mode_active_);
    });

    connect(conversations_list_, &QListWidget::itemSelectionChanged, this, [this]() {
        const QString id = SelectedConversation();
        SyncContactSelection(id);
        SetContactsMode(false);
        RefreshConversationSelectionStyles();
        UpdateThreadHeader();
        UpdateCallControls();
        if (id.isEmpty()) {
            SetTimelineHasMessages(false);
            return;
        }

        messages_list_->clear();
        SetTimelineHasMessages(false);
        emit ConversationSelected(id);
    });

    connect(contacts_panel_list_, &QListWidget::itemSelectionChanged, this, [this]() {
        const auto* selected = contacts_panel_list_->currentItem();
        if (selected == nullptr) {
            return;
        }
        const QString id = selected->data(kRoleConversationId).toString();
        if (id.isEmpty()) {
            return;
        }
        SetSelectedConversation(id);
        SetContactsMode(false);
        messages_list_->clear();
        SetTimelineHasMessages(false);
        emit ConversationSelected(id);
    });

    SetContactsMode(true);
    UpdatePresenceIndicator("offline");
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
    return item->data(kRoleConversationId).toString();
}

void ChatWidget::SetSelectedConversation(const QString& conversation_id) {
    QSignalBlocker signal_blocker(conversations_list_);
    QSignalBlocker contact_signal_blocker(contacts_panel_list_);
    if (conversation_id.isEmpty()) {
        conversations_list_->setCurrentItem(nullptr);
        contacts_panel_list_->setCurrentItem(nullptr);
        RefreshConversationSelectionStyles();
        UpdateThreadHeader();
        UpdateCallControls();
        return;
    }

    for (int index = 0; index < conversations_list_->count(); ++index) {
        auto* item = conversations_list_->item(index);
        if (item->data(kRoleConversationId).toString() != conversation_id) {
            continue;
        }
        conversations_list_->setCurrentItem(item);
        break;
    }
    for (int index = 0; index < contacts_panel_list_->count(); ++index) {
        auto* item = contacts_panel_list_->item(index);
        if (item->data(kRoleConversationId).toString() != conversation_id) {
            continue;
        }
        contacts_panel_list_->setCurrentItem(item);
        break;
    }

    RefreshConversationSelectionStyles();
    UpdateThreadHeader();
    UpdateCallControls();
}

QWidget* ChatWidget::CreateConversationItemWidget(
    const ConversationListItemView& item,
    bool selected,
    bool removable) {
    auto* row = new QWidget();
    row->setObjectName("conversationRow");
    row->setProperty("selected", selected);

    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(8);

    auto* status_dot = new QLabel(row);
    status_dot->setObjectName("presenceDot");
    const QString normalized_status = NormalizePresenceStatus(item.status);
    status_dot->setProperty("state", normalized_status);
    status_dot->setFixedSize(10, 10);
    status_dot->setStyleSheet(
        QString(
            "background:%1;"
            "border-radius:5px;"
            "min-width:10px;max-width:10px;"
            "min-height:10px;max-height:10px;")
            .arg(PresenceColorForStatus(normalized_status)));
    layout->addWidget(status_dot, 0, Qt::AlignTop);

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
    if (removable) {
        auto* remove_button = new QPushButton("x", row);
        remove_button->setObjectName("conversationRemoveButton");
        remove_button->setFixedSize(18, 18);
        remove_button->setFocusPolicy(Qt::NoFocus);
        remove_button->setToolTip(
            item.conversation_type.trimmed().toLower() == "group"
                ? "Leave group"
                : "Remove DM");
        connect(remove_button, &QPushButton::clicked, row, [this, item]() {
            emit ConversationRemoveRequested(
                item.id,
                item.conversation_type.trimmed().toLower(),
                item.can_manage_members,
                item.title.trimmed());
        });
        layout->addWidget(remove_button, 0, Qt::AlignTop);
    }

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

    QString file_name;
    QByteArray file_bytes;
    qint64 file_size = 0;
    const bool is_file = DecodeFileMessageBody(message.body, &file_name, &file_bytes, &file_size);
    if (is_file) {
        auto* file_label = new QLabel(
            QString("Encrypted file: %1 (%2)").arg(file_name, HumanFileSize(file_size)),
            bubble);
        file_label->setObjectName("messageBody");
        file_label->setWordWrap(true);
        bubble_layout->addWidget(file_label);

        auto* save_button = new QPushButton("Save file", bubble);
        save_button->setObjectName("secondaryButton");
        QObject::connect(save_button, &QPushButton::clicked, bubble, [file_name, file_bytes, bubble]() {
            const QString target_path = QFileDialog::getSaveFileName(bubble, "Save file", file_name);
            if (target_path.trimmed().isEmpty()) {
                return;
            }
            QFile output(target_path);
            if (!output.open(QIODevice::WriteOnly)) {
                return;
            }
            output.write(file_bytes);
            output.close();
        });
        bubble_layout->addWidget(save_button, 0, Qt::AlignLeft);
    } else {
        auto* body = new QLabel(message.body, bubble);
        body->setObjectName("messageBody");
        body->setWordWrap(true);
        body->setTextInteractionFlags(Qt::TextSelectableByMouse);
        bubble_layout->addWidget(body);
    }

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
    QSignalBlocker contact_signal_blocker(contacts_panel_list_);

    conversations_list_->clear();
    contacts_panel_list_->clear();
    for (const auto& item : conversations) {
        auto* dm_item = new QListWidgetItem(conversations_list_);
        dm_item->setData(kRoleConversationId, item.id);
        dm_item->setData(kRoleTitle, item.title);
        dm_item->setData(kRoleSubtitle, item.subtitle);
        dm_item->setData(kRoleStatus, item.status);
        dm_item->setData(kRoleConversationType, item.conversation_type);
        dm_item->setData(kRoleCanManageMembers, item.can_manage_members);
        dm_item->setData(kRolePeerAddress, item.peer_address);
        dm_item->setData(kRoleGroupName, item.group_name);
        dm_item->setData(kRoleMemberCount, item.member_count);
        dm_item->setSizeHint(QSize(200, 64));
        auto* dm_row = CreateConversationItemWidget(item, item.id == selected_id, true);
        conversations_list_->setItemWidget(dm_item, dm_row);

        auto* contact_item = new QListWidgetItem(contacts_panel_list_);
        contact_item->setData(kRoleConversationId, item.id);
        contact_item->setData(kRoleTitle, item.title);
        contact_item->setData(kRoleSubtitle, item.subtitle);
        contact_item->setData(kRoleStatus, item.status);
        contact_item->setData(kRoleConversationType, item.conversation_type);
        contact_item->setData(kRoleCanManageMembers, item.can_manage_members);
        contact_item->setData(kRolePeerAddress, item.peer_address);
        contact_item->setData(kRoleGroupName, item.group_name);
        contact_item->setData(kRoleMemberCount, item.member_count);
        contact_item->setSizeHint(QSize(200, 54));
        ConversationListItemView contact_view = item;
        contact_view.subtitle = QString("Status: %1").arg(NormalizePresenceStatus(item.status));
        auto* contact_row = CreateConversationItemWidget(contact_view, item.id == selected_id, false);
        contacts_panel_list_->setItemWidget(contact_item, contact_row);

        if (!selected_id.isEmpty() && item.id == selected_id) {
            conversations_list_->setCurrentItem(dm_item);
            contacts_panel_list_->setCurrentItem(contact_item);
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
    if (contacts_mode_active_) {
        thread_title_label_->setText("Contacts");
        thread_title_label_->setReadOnly(true);
        thread_title_label_->setFocusPolicy(Qt::NoFocus);
        thread_title_label_->setCursor(Qt::ArrowCursor);
        thread_title_label_->setToolTip({});
        return;
    }
    const auto* selected = conversations_list_->currentItem();
    if (selected == nullptr) {
        thread_title_label_->setText("Select a conversation");
        thread_title_label_->setReadOnly(true);
        thread_title_label_->setFocusPolicy(Qt::NoFocus);
        thread_title_label_->setCursor(Qt::ArrowCursor);
        thread_title_label_->setToolTip({});
        empty_state_label_->setText("Select a conversation to start chatting.");
        return;
    }

    const QString title = selected->data(kRoleTitle).toString();
    thread_title_label_->setText(title.isEmpty() ? "Direct message" : title);
    const QString conversation_type = selected->data(kRoleConversationType).toString().trimmed().toLower();
    const bool can_manage_members = selected->data(kRoleCanManageMembers).toBool();
    const bool owner_group = conversation_type == "group" && can_manage_members;
    thread_title_label_->setReadOnly(!owner_group);
    thread_title_label_->setFocusPolicy(owner_group ? Qt::ClickFocus : Qt::NoFocus);
    thread_title_label_->setCursor(owner_group ? Qt::IBeamCursor : Qt::ArrowCursor);
    thread_title_label_->setToolTip(
        owner_group ? "Press Enter to rename this group." : QString());
    if (messages_list_->count() == 0) {
        empty_state_label_->setText("No messages yet. Send the first encrypted message.");
    }
}

void ChatWidget::RefreshConversationSelectionStyles() {
    RefreshListSelectionStyles(conversations_list_);
    RefreshListSelectionStyles(contacts_panel_list_);
}

void ChatWidget::RefreshListSelectionStyles(QListWidget* list) {
    if (list == nullptr) {
        return;
    }
    for (int index = 0; index < list->count(); ++index) {
        auto* item = list->item(index);
        auto* row = list->itemWidget(item);
        if (row == nullptr) {
            continue;
        }
        const bool selected = list->currentRow() == index;
        row->setProperty("selected", selected);
        row->style()->unpolish(row);
        row->style()->polish(row);
    }
}

void ChatWidget::SyncContactSelection(const QString& conversation_id) {
    QSignalBlocker signal_blocker(contacts_panel_list_);
    if (conversation_id.isEmpty()) {
        contacts_panel_list_->setCurrentItem(nullptr);
        return;
    }

    for (int index = 0; index < contacts_panel_list_->count(); ++index) {
        auto* item = contacts_panel_list_->item(index);
        if (item->data(kRoleConversationId).toString() != conversation_id) {
            continue;
        }
        contacts_panel_list_->setCurrentItem(item);
        return;
    }
    contacts_panel_list_->setCurrentItem(nullptr);
}

void ChatWidget::SetContactsMode(bool enabled) {
    contacts_mode_active_ = enabled;
    if (content_stack_ != nullptr) {
        content_stack_->setCurrentWidget(contacts_mode_active_ ? contacts_panel_ : chat_panel_);
    }
    UpdateContactsButtonState();
    UpdateThreadHeader();
    UpdateCallControls();
}

void ChatWidget::UpdateContactsButtonState() {
    if (contacts_button_ != nullptr) {
        contacts_button_->setChecked(contacts_mode_active_);
    }
}

QString ChatWidget::SelectedConversationType() const {
    const auto* item = conversations_list_->currentItem();
    if (item == nullptr) {
        return "direct";
    }
    const QString value = item->data(kRoleConversationType).toString().trimmed().toLower();
    return value.isEmpty() ? "direct" : value;
}

bool ChatWidget::SelectedConversationCanManageMembers() const {
    const auto* item = conversations_list_->currentItem();
    if (item == nullptr) {
        return false;
    }
    return item->data(kRoleCanManageMembers).toBool();
}

void ChatWidget::UpdateCallControls() {
    const QString state = call_state_.state.trimmed().toLower();
    const bool has_selection = !SelectedConversation().isEmpty();
    const bool in_contacts_mode = contacts_mode_active_;
    const QString conversation_type = SelectedConversationType();
    const bool selected_direct = conversation_type == "direct";
    const bool selected_group = conversation_type == "group";
    const bool selected_owner_managed_group = selected_group && SelectedConversationCanManageMembers();
    const bool panel_visible =
        state == "incoming_ringing" || state == "outgoing_ringing" || state == "active" || state == "ending";
    const bool idle_and_chat_ready = !in_contacts_mode && has_selection && state == "idle" && !panel_visible;

    call_status_label_->setText(FormatCallStatusText(call_state_));
    call_status_label_->setProperty("state", CallStatusStyleState(call_state_));
    call_status_label_->style()->unpolish(call_status_label_);
    call_status_label_->style()->polish(call_status_label_);

    call_panel_->setVisible(panel_visible);
    call_button_->setVisible(idle_and_chat_ready);
    call_button_->setEnabled(has_selection && !in_contacts_mode);
    group_button_->setVisible(idle_and_chat_ready && selected_direct);
    group_button_->setEnabled(idle_and_chat_ready && selected_direct);
    invite_button_->setVisible(idle_and_chat_ready && selected_owner_managed_group);
    invite_button_->setEnabled(idle_and_chat_ready && selected_owner_managed_group);
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

    while (call_participants_layout_ != nullptr && call_participants_layout_->count() > 0) {
        QLayoutItem* item = call_participants_layout_->takeAt(0);
        if (item == nullptr) {
            continue;
        }
        if (item->widget() != nullptr) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    const bool show_participants = state == "active" && !call_state_.participants.empty();
    if (show_participants && call_participants_layout_ != nullptr) {
        for (const auto& participant : call_state_.participants) {
            QString label = participant.label.trimmed();
            if (label.isEmpty()) {
                label = participant.user_address.trimmed();
            }
            if (label.isEmpty()) {
                label = participant.self ? "You" : "Member";
            }
            auto* chip = new QLabel(label, call_participants_panel_);
            chip->setObjectName("callParticipantChip");
            chip->setProperty("self", participant.self);
            call_participants_layout_->addWidget(chip);
        }
        call_participants_layout_->addStretch(1);
    }
    call_participants_panel_->setVisible(show_participants);

    QChar initial = peer_title.isEmpty() ? QChar('#') : peer_title.at(0).toUpper();
    call_panel_avatar_->setText(QString(initial));

    call_panel_->setProperty("state", CallStatusStyleState(call_state_));
    call_panel_->style()->unpolish(call_panel_);
    call_panel_->style()->polish(call_panel_);
}

void ChatWidget::ShowGroupInviteDialog(const GroupInvitePickerView& picker) {
    QDialog dialog(this);
    dialog.setWindowTitle("Invite Contacts");
    dialog.setModal(true);
    dialog.resize(500, 600);

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);

    const int remaining_slots = std::max(0, picker.remaining_slots);
    auto* header_label = new QLabel(QString("You can add %1 more members.").arg(remaining_slots), &dialog);
    header_label->setWordWrap(true);
    layout->addWidget(header_label);

    auto* search_input = new QLineEdit(&dialog);
    search_input->setPlaceholderText("Type username or address");
    layout->addWidget(search_input);

    auto* list = new QListWidget(&dialog);
    list->setSelectionMode(QAbstractItemView::NoSelection);
    list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    layout->addWidget(list, 1);

    auto* hint_label = new QLabel(&dialog);
    hint_label->setWordWrap(true);
    layout->addWidget(hint_label);

    auto* actions = new QHBoxLayout();
    actions->addStretch(1);
    auto* cancel_button = new QPushButton("Cancel", &dialog);
    cancel_button->setObjectName("secondaryButton");
    auto* invite_action_button = new QPushButton("Invite", &dialog);
    invite_action_button->setObjectName("primaryButton");
    actions->addWidget(cancel_button);
    actions->addWidget(invite_action_button);
    layout->addLayout(actions);

    std::vector<QString> selected_addresses;
    selected_addresses.reserve(static_cast<std::size_t>(remaining_slots));
    const auto is_selected = [&selected_addresses](const QString& value) {
        return std::find(selected_addresses.begin(), selected_addresses.end(), value) != selected_addresses.end();
    };
    const auto update_action_state = [&]() {
        invite_action_button->setEnabled(remaining_slots > 0 && !selected_addresses.empty());
    };

    const auto repopulate = [&]() {
        const QString query = search_input->text().trimmed().toLower();
        list->clear();
        int shown = 0;
        for (const auto& candidate : picker.candidates) {
            const QString haystack = QString("%1 %2 %3")
                                         .arg(candidate.title, candidate.subtitle, candidate.peer_address)
                                         .trimmed()
                                         .toLower();
            if (!query.isEmpty() && !haystack.contains(query)) {
                continue;
            }
            auto* item = new QListWidgetItem(
                QString("%1\n%2").arg(candidate.title, candidate.subtitle.isEmpty() ? candidate.peer_address : candidate.subtitle),
                list);
            item->setData(Qt::UserRole, candidate.peer_address);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
            item->setCheckState(is_selected(candidate.peer_address) ? Qt::Checked : Qt::Unchecked);
            shown += 1;
        }

        if (remaining_slots <= 0) {
            hint_label->setText("Member limit reached. No more invites can be sent.");
        } else if (picker.candidates.empty()) {
            hint_label->setText("No direct-message contacts are available to invite.");
        } else if (shown == 0) {
            hint_label->setText("No contacts match your search.");
        } else {
            hint_label->setText(QString("Select up to %1 contact(s).").arg(remaining_slots));
        }
        update_action_state();
    };

    connect(search_input, &QLineEdit::textChanged, &dialog, [&]() {
        repopulate();
    });
    connect(list, &QListWidget::itemChanged, &dialog, [&](QListWidgetItem* item) {
        if (item == nullptr) {
            return;
        }
        const QString address = item->data(Qt::UserRole).toString().trimmed().toLower();
        if (address.isEmpty()) {
            return;
        }
        if (item->checkState() == Qt::Checked) {
            if (is_selected(address)) {
                return;
            }
            if (static_cast<int>(selected_addresses.size()) >= remaining_slots) {
                item->setCheckState(Qt::Unchecked);
                hint_label->setText(QString("You can only invite up to %1 contact(s).").arg(remaining_slots));
                return;
            }
            selected_addresses.push_back(address);
        } else {
            selected_addresses.erase(
                std::remove(selected_addresses.begin(), selected_addresses.end(), address),
                selected_addresses.end());
        }
        update_action_state();
    });
    connect(cancel_button, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(invite_action_button, &QPushButton::clicked, &dialog, [&]() {
        if (selected_addresses.empty()) {
            return;
        }
        emit InviteGroupMembersRequested(selected_addresses);
        dialog.accept();
    });

    repopulate();
    update_action_state();
    dialog.exec();
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
    if (contacts_mode_active_ && call_state_.state.trimmed().toLower() != "idle") {
        SetContactsMode(false);
    }
    UpdateCallControls();
}

void ChatWidget::SetUserStatus(const QString& status) {
    const QString normalized = NormalizePresenceStatus(status);
    QSignalBlocker blocker(presence_combo_);
    int target_index = -1;
    for (int index = 0; index < presence_combo_->count(); ++index) {
        if (NormalizePresenceStatus(presence_combo_->itemData(index).toString()) == normalized) {
            target_index = index;
            break;
        }
    }
    if (target_index < 0) {
        target_index = 0;
    }
    presence_combo_->setCurrentIndex(target_index);
    UpdatePresenceIndicator(normalized);
}

void ChatWidget::UpdatePresenceIndicator(const QString& status) {
    if (presence_indicator_ == nullptr) {
        return;
    }
    const QString normalized = NormalizePresenceStatus(status);
    presence_indicator_->setProperty("state", normalized);
    presence_indicator_->setStyleSheet(
        QString(
            "background:%1;"
            "border-radius:5px;"
            "min-width:10px;max-width:10px;"
            "min-height:10px;max-height:10px;")
            .arg(PresenceColorForStatus(normalized)));
    presence_indicator_->style()->unpolish(presence_indicator_);
    presence_indicator_->style()->polish(presence_indicator_);
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
