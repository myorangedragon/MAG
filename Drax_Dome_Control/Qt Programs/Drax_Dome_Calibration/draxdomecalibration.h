#ifndef DRAXDOMECALIBRATION_H
#define DRAXDOMECALIBRATION_H

#include <QMainWindow>
#include <QComboBox>
#include <QPushButton>
#include <QSerialPort>
#include <QLabel>

QT_BEGIN_NAMESPACE
namespace Ui { class DraxDomeCalibration; }
QT_END_NAMESPACE

class DraxDomeCalibration : public QMainWindow
{
    Q_OBJECT

public:
    DraxDomeCalibration(QWidget *parent = nullptr);
    ~DraxDomeCalibration();

private:
    Ui::DraxDomeCalibration *ui;

    QSerialPort *ser;
    QComboBox   *combobox_com_list;
    QPushButton *button_connect;
    QPushButton *button_cal;
    QLabel      *label_status;
    QLineEdit   *lineEdit_calAngle;
    bool        calibrating;
    uint16_t    cal_timer;


private slots:
        void do_one_second_timer();
        void do_connect();
        void do_cal();
        void read_serial_data();

};
#endif // DRAXDOMECALIBRATION_H
