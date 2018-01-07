
#include <assert.h>
#include <QDebug>
#include "nodemodel.h"
#include "database.h"

using namespace std;

NodeModel::NodeModel()
    : root_{make_shared<Root>()}
{
    loadData();
}

QModelIndex NodeModel::addNode(const QModelIndex &parentIndex, std::shared_ptr<Node> node)
{
    // Insert to database
    try {
        flushNode(*node);
    } catch(const std::exception& ex) {
        qWarning() << "Failed to add node with name " << node->name
                   << ": " << ex.what();

        // TODO: Add error dialog
        return {};
    }

    // Update in-memory image of model
    int children = rowCount(parentIndex);
    beginInsertRows(parentIndex, children, children);
    auto parent = static_cast<Node *>(parentIndex.internalPointer());
    if (parent == nullptr) {
        parent = getRootNode();
    }
    parent->addChild(move(node));
    endInsertRows();

    return index(children, 0, parentIndex);
}

QModelIndex NodeModel::index(int row, int column,
                             const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent)) {
        return {};
    }

    Node *parent_item = parent.isValid()
            ? static_cast<Node *>(parent.internalPointer())
            : root_.get();

    try {
        auto child = parent_item->getChild(static_cast<size_t>(row));
        assert(child != nullptr);
        return createIndex(row, column, child);
    } catch(const std::out_of_range&) {
        qWarning() << "Invalid child index " << row
                   << " for node " << parent_item->id;
    }

    return {};
}

QModelIndex NodeModel::parent(const QModelIndex &child) const
{
    if (!child.isValid()) {
        return {};
    }

    auto node = static_cast<Node *>(child.internalPointer());
    auto parent = node->getParent();
    if (parent == root_.get()) {
        return {};
    }

    return createIndex(parent->getRow(), 0, parent);
}

int NodeModel::rowCount(const QModelIndex &parent) const
{
    Node *parent_item = parent.isValid()
            ? static_cast<Node *>(parent.internalPointer())
            : root_.get();

    const_cast<NodeModel *>(this)->fetchChildren(*parent_item);
    return static_cast<int>(parent_item->getNumChildren());
}

int NodeModel::columnCount(const QModelIndex&) const
{
    return 1;
}

bool NodeModel::hasChildren(const QModelIndex &parent) const
{
    return rowCount(parent) > 0;
}

QVariant NodeModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return {};
    }

    const auto node = static_cast<const Node *>(index.internalPointer());

    switch(role) {
        case Qt::DisplayRole:
        case Qt::EditRole:
            return node->name;
        case Qt::DecorationRole:
            return node->getIcon({16,16});

    }

    return {};
}

bool NodeModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (role != Qt::EditRole)
        return false;

    const auto valueStr = value.toString();
    if (valueStr.isEmpty()) {
        return false;
    }

    auto node = static_cast<Node *>(index.internalPointer());
    assert(node);
    assert(node->getType() != Node::Type::ROOT);
    node->name = valueStr;
    try {
        flushNode(*node);
    } catch(const std::exception& ex) {
        qWarning() << "Failed to reame node #" << node->id << " to "
                   << valueStr << ". Error: " << ex.what();
        return false;
    }

    // FIXME: If we emit, the application crash
    //emit dataChanged(index, index);
    return true;
}

Qt::ItemFlags NodeModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::ItemIsEnabled;
    }

    return Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable;
}

void NodeModel::loadData()
{
    root_->clearChildren();
    fetchChildren(*root_);
}

void NodeModel::fetchChildren(Node &parent)
{
    if (parent.isFetched) {
        return;
    }

    QSqlQuery query;

    if (parent.id > 0) {
        query.prepare("SELECT id, name, type, descr, active, charge FROM node where parent=? ORDER BY name");
        query.addBindValue(parent.id);
    } else {
        query.prepare("SELECT id, name, type, descr, active, charge FROM node where parent IS NULL ORDER BY name");
    }
    if (!query.exec()) {
        qWarning() << "Failed to fetch from database: " << query.lastError();
    }

    while(query.next()) {
        const int nt = query.value(2).toInt();
        shared_ptr<Node> node;

        switch(nt) {
            case static_cast<int>(Node::Type::FOLDER):
                node = make_shared<Folder>(parent.shared_from_this());
            break;

            case static_cast<int>(Node::Type::CUSTOMER):
                node = make_shared<Customer>(parent.shared_from_this());
            break;

            case static_cast<int>(Node::Type::PROJECT):
                node = make_shared<Project>(parent.shared_from_this());
            break;

            case static_cast<int>(Node::Type::TASK):
                node = make_shared<Task>(parent.shared_from_this());
            break;

            default:
                qWarning() << "Ignoring unknown node type " << nt << "from database";
                continue;
        }

        node->id = query.value(0).toInt();
        node->name = query.value(1).toString();
        node->descr = query.value(3).toString();
        node->active = query.value(4).toBool();
        node->charge = query.value(5).toInt();

        parent.addChild(move(node));
    }

    parent.isFetched = true;
}

void Node::addCustomer()
{
    auto node = std::make_shared<Customer>(shared_from_this());
    node->name = "New Customer";

    // Always add at end
    addChild(move(node));
}

QVariant Node::getNodeIcon(QString name, QSize size) const
{
    QString path(":/res/icons/");
    path += name;

    return QIcon(path).pixmap(size);
}

void NodeModel::flushNode(Node &node)
{
    QSqlQuery query;
    QString sql, fields;

    const bool do_update = node.id != 0;

    if (do_update) {
        sql = "UPDATE node SET "
              "name=:name, type=:type, descr=:descr, active=:active, charge=:charge, parent=:parent "
              "WHERE id = :id";
    } else {
        sql = "INSERT INTO node (name, type, descr, active, charge, parent) "
              "VALUES (:name, :type, :descr, :active, :charge, :parent)";
    }

    int parent_id = 0;
    auto parent = node.getParent();
    if (parent && (parent->getType() != Node::Type::ROOT)) {
        parent_id = parent->id;
    }

    query.prepare(sql);
    query.bindValue(":name", node.name);
    query.bindValue(":type", node.getTypeId());
    query.bindValue(":descr", node.descr);
    query.bindValue(":active", node.active);
    query.bindValue(":charge", node.charge);

    if (parent_id) {
        query.bindValue(":parent", parent_id);
    } else {
        query.bindValue(":parent", QVariant::Int);
    }

    if (do_update) {
        query.bindValue(":id", node.id);
    }

    if (!query.exec()) {
        qWarning() << "flushNode failed. error:  " << query.lastError();
        throw runtime_error("Failed to insert/update node");
    } else {
        if (!do_update) {
            node.id = query.lastInsertId().toInt();
        }
        qDebug() << "Flushed node #" << node.id << " " << node.name << " with parent " << parent_id;
    }
}
