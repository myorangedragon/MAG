#ifndef DRAXTELESCOPECONTROL_H
#define DRAXTELESCOPECONTROL_H

#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui {
class DraxTelescopeControl;
}
QT_END_NAMESPACE

class DraxTelescopeControl : public QMainWindow
{
    Q_OBJECT

public:
    explicit DraxTelescopeControl(QWidget *parent = nullptr);
    ~DraxTelescopeControl() override;

private:
    Ui::DraxTelescopeControl *ui;

    button_up               = findChild<QPushButton*>("pushButton_up");
    button_down             = findChild<QPushButton*>("pushButton_down");
    button_left             = findChild<QPushButton*>("pushButton_left");
    button_right            = findChild<QPushButton*>("pushButton_right");
    button_upFast           = findChild<QPushButton*>("pushButton_upFast");
    button_downFast         = findChild<QPushButton*>("pushButton_downFast");
    button_leftFast         = findChild<QPushButton*>("pushButton_leftFast");
    button_rightFast        = findChild<QPushButton*>("pushButton_rightFast");

};
#endif // DRAXTELESCOPECONTROL_H
