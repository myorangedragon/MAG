#include "draxtelescopecontrol.h"
#include "./ui_draxtelescopecontrol.h"

DraxTelescopeControl::DraxTelescopeControl(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::DraxTelescopeControl)
{
    ui->setupUi(this);

    /* ------------------------------------------ */
    /* ------------- Buttons -------------------- */
    /* ------------------------------------------ */
    button_up               = findChild<QPushButton*>("pushButton_up");
    button_down             = findChild<QPushButton*>("pushButton_down");
    button_left             = findChild<QPushButton*>("pushButton_left");
    button_right            = findChild<QPushButton*>("pushButton_right");
    button_upFast           = findChild<QPushButton*>("pushButton_upFast");
    button_downFast         = findChild<QPushButton*>("pushButton_downFast");
    button_leftFast         = findChild<QPushButton*>("pushButton_leftFast");
    button_rightFast        = findChild<QPushButton*>("pushButton_rightFast");
    connect(button_up,        SIGNAL(clicked()), this, SLOT(do_up()));
    connect(button_down,      SIGNAL(clicked()), this, SLOT(do_down()));
    connect(button_left,      SIGNAL(clicked()), this, SLOT(do_left()));
    connect(button_right,     SIGNAL(clicked()), this, SLOT(do_right()));
    connect(button_upFast,    SIGNAL(clicked()), this, SLOT(do_upFast()));
    connect(button_downFast,  SIGNAL(clicked()), this, SLOT(do_downFast()));
    connect(button_leftFast,  SIGNAL(clicked()), this, SLOT(do_leftFast()));
    connect(button_rightFast, SIGNAL(clicked()), this, SLOT(do_rightFast()));

    /* ------------------------------------------ */
    /* -------------- Labels -------------------- */
    /* ------------------------------------------ */
    QPixmap pix;
    QLabel *label_pic = findChild<QLabel*>("label_drax_image");
    pix.load("Drax_dome.png");
    /* scale pixmap to fit in label */
    pix = pix.scaled(label_pic->size(),Qt::KeepAspectRatio);
    label_pic->setPixmap(pix);


}

DraxTelescopeControl::~DraxTelescopeControl()
{
    delete ui;
}
