#pragma once

#include <QWidget>

class QLineEdit;
class QPushButton;

namespace blackwire {

class LoginWidget final : public QWidget {
    Q_OBJECT

public:
    explicit LoginWidget(QWidget* parent = nullptr);

    void SetBaseUrl(const QString& base_url);
    QString BaseUrl() const;
    QString Username() const;
    QString Password() const;

signals:
    void LoginRequested();
    void RegisterRequested();

private:
    QLineEdit* base_url_input_;
    QLineEdit* username_input_;
    QLineEdit* password_input_;
    QPushButton* login_button_;
    QPushButton* register_button_;
};

}  // namespace blackwire
