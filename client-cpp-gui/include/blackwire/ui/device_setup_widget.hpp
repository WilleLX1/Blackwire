#pragma once

#include <QWidget>

class QLineEdit;
class QPushButton;

namespace blackwire {

class DeviceSetupWidget final : public QWidget {
    Q_OBJECT

public:
    explicit DeviceSetupWidget(QWidget* parent = nullptr);

    QString DeviceLabel() const;

signals:
    void DeviceSetupRequested();

private:
    QLineEdit* device_label_input_;
    QPushButton* setup_button_;
};

}  // namespace blackwire
