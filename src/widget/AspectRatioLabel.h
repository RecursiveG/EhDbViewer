#ifndef ASPECTRATIOLABEL_H
#define ASPECTRATIOLABEL_H
// See https://stackoverflow.com/a/43936590/2515783

#include <QLabel>

class AspectRatioLabel : public QLabel {
  public:
    explicit AspectRatioLabel(QWidget *parent = nullptr,
                              Qt::WindowFlags f = Qt::WindowFlags());
    ~AspectRatioLabel();

  public slots:
    void setPixmap(const QPixmap &pm);

  protected:
    void resizeEvent(QResizeEvent *event) override;

  private:
    void updateMargins();

    long long pixmapWidth = 0;
    long long pixmapHeight = 0;
};

#endif // ASPECTRATIOLABEL_H
