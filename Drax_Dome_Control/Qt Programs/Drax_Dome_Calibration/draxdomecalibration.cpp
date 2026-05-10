#include "draxdomecalibration.h"
#include "ui_draxdomecalibration.h"

#include <QSerialPortInfo>
#include <QTimer>

#define MAX_CAL_TIME 20

DraxDomeCalibration::DraxDomeCalibration(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::DraxDomeCalibration)
{
    ui->setupUi(this);

    /* ------------------------------------------ */
    /* ------------- Buttons -------------------- */
    /* ------------------------------------------ */
    button_connect         = findChild<QPushButton*>("pushButton_connect");
    button_cal             = findChild<QPushButton*>("pushButton_Cal");
    connect(button_connect,     SIGNAL(clicked()), this, SLOT(do_connect()));
    connect(button_cal,         SIGNAL(clicked()), this, SLOT(do_cal()));

    /* ------------------------------------------ */
    /* -------------- Labels -------------------- */
    /* ------------------------------------------ */
    label_status = findChild<QLabel*>("label_status");
    label_status->setText("Please select a Com port and press connect");

    QPixmap pix;
    QLabel *label_pic = findChild<QLabel*>("label_drax_image");
    pix.load("Drax_dome.png");
    /* scale pixmap to fit in label */
    pix = pix.scaled(label_pic->size(),Qt::KeepAspectRatio);
    label_pic->setPixmap(pix);

    /* ------------------------------------------ */
    /* -------------- LineEdits ----------------- */
    /* ------------------------------------------ */
    lineEdit_calAngle = findChild<QLineEdit*>("lineEdit_calAngle");

    /* ------------------------------------------ */
    /* -------------- ComboBoxes ---------------- */
    /* ------------------------------------------ */
    combobox_com_list = findChild<QComboBox*>("comboBox_com_list");
    /* fill the com port list on startup */
    foreach (const QSerialPortInfo port, QSerialPortInfo::availablePorts()) {
        combobox_com_list->addItem(port.portName());
    }

    /* ------------------------------------------ */
    /* ------------- Timers --------------------- */
    /* ------------------------------------------ */
    /* timer to poll the com ports to see if they have changed */
    QTimer *timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(do_one_second_timer()));
    timer->start(1000);

    /* ------------------------------------------ */
    /* ------------- Local Setup ---------------- */
    /* ------------------------------------------ */
    calibrating = false;
}

DraxDomeCalibration::~DraxDomeCalibration()
{
    delete ui;
}

void DraxDomeCalibration::do_cal() {
    char msg[17];
    float angle = lineEdit_calAngle->text().toFloat();

    /* send "CALIBRATE_xxx.x#" to Dome controller */
    sprintf(msg, "CALIBRATE_%05.1f#", angle);
    ser->write(msg, 17);
    qDebug() << "    sent: " << msg;

    label_status->setText("Sent Cal request to Dome - please wait");
    calibrating = true;
    cal_timer = 0;
}

void DraxDomeCalibration::read_serial_data() {
    char tx_packet[50];

    ser->readLine(tx_packet, 50);
    qDebug() << "received: " << tx_packet;
    if (tx_packet[0]=='0') {
        label_status->setText("Dome Cal failed");
        calibrating = false;
        button_cal->setStyleSheet(" background-color : Red; color: Black");
    } else if (tx_packet[0]=='1') {
        label_status->setText("Dome Cal started, this may take some time");
        calibrating = true;
    } else if (tx_packet[0]=='2') {
        label_status->setText("Dome Cal Finished SUCCESS");
        calibrating = false;
        button_cal->setStyleSheet(" background-color : Green; color: White");
    } else if (tx_packet[0]=='3') {
        label_status->setText("Dome Cal finished but FAILED");
        button_cal->setStyleSheet(" background-color : Red; color: Black");
        calibrating = false;
    }
}

void DraxDomeCalibration::do_connect()
{
    /* open com Port */
    QString com_port_name = combobox_com_list->currentText();
    ser = new QSerialPort(com_port_name);
    ser->setParity(QSerialPort::NoParity);
    ser->setBaudRate(QSerialPort::Baud115200);
    ser->setDataBits(QSerialPort::Data8);
    ser->setStopBits(QSerialPort::OneStop);
    ser->setFlowControl(QSerialPort::NoFlowControl);

    connect(ser, &QSerialPort::readyRead, this, &DraxDomeCalibration::read_serial_data);

    if (ser->open(QIODevice::ReadWrite)) {
        ser->flush();
        label_status->setText("Please specify the home angle and press Calibrate");
        button_connect->setStyleSheet(" background-color : Green; color: White");
    } else {
        label_status->setText("Failed to connect to that Com Port");
        button_connect->setStyleSheet(" background-color : Red; color: Black");
    }
}

void DraxDomeCalibration::do_one_second_timer()
{
    static bool cal_on = false;

    /* first we update the com list f anything has changed */
    static QList<QSerialPortInfo> old_list(QSerialPortInfo::availablePorts());
    QList<QSerialPortInfo> new_list=QSerialPortInfo::availablePorts();

    if (new_list.size()!=old_list.size()) {
        combobox_com_list->clear();
        combobox_com_list->addItem("Manual");
        foreach (const QSerialPortInfo port, new_list) {
            combobox_com_list->addItem(port.portName());
        }
        old_list=new_list;
    } else {
        /* it might be that one has appeared and one vanished so same size but different elements */
        for (int pos=0;pos<new_list.size();pos++) {
            if (new_list.at(pos).portName()!=old_list.at(pos).portName()) {
                combobox_com_list->clear();
                combobox_com_list->addItem("Manual");
                foreach (const QSerialPortInfo port, new_list) {
                    combobox_com_list->addItem(port.portName());
                }
                old_list=new_list;
                break;
            }
        }
    }

    /* we also want to handle the alive indicator for when we are calibrating */
    cal_timer++;

    if (calibrating) {
        if (cal_on) {
            cal_on = false;
            button_cal->setStyleSheet(" background-color : LightGrey; color: Black");
        } else {
            cal_on = true;
            button_cal->setStyleSheet(" background-color : Green; color: White");
        }
        if (cal_timer == MAX_CAL_TIME) {
            calibrating = false;
            button_cal->setStyleSheet(" background-color : Red; color: Black");
            label_status->setText("Calibration timed out");
        }
    }
}
