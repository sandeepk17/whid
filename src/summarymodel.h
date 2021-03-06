#ifndef SUMMARYMODEL_H
#define SUMMARYMODEL_H

#include <vector>
#include <deque>
#include <memory>

#include <QAbstractTableModel>
#include <QDate>

class NodeModel;
class Node;

class SummaryModel : public QAbstractTableModel
{
    Q_OBJECT

    struct Row {
        QString name;
        std::vector<QVariant> cols;
        std::weak_ptr<Node> node;
    };

public:
    enum class Mode {
        WEEK
    };

    enum class When {
        CURRENT,
        PREVIOUS,
        WEEK_NUMBER,
    };

    SummaryModel(NodeModel& nm);

    const QDate& getWeekSelection() const {
        return weekSelection;
    }

public slots:
    void modeChanged(const Mode mode);
    void whenChanged(const When when);
    void dataChanged();
    void setSelectedWeek(const QDate& date);

signals:
    void selectionTextChanged(const QString& text);

    // QAbstractItemModel interface
public:
    int rowCount(const QModelIndex &parent) const override;
    int columnCount(const QModelIndex &parent) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    //Qt::ItemFlags flags(const QModelIndex &index) const override;

private:
    void loadData();
    void loadWeek();
    void getDateSpan(QDate& firstday, QDate& lastday);
    void atMidnight();
    void schedulaAtMidnight();

    std::vector<QString> headers_;
    std::deque<std::unique_ptr<Row>> rows_;
    Mode mode_ = Mode::WEEK;
    When when_ = When::CURRENT;
    QDate weekSelection = QDate::currentDate();
    NodeModel& node_model_;
};

#endif // SUMMARYMODEL_H
