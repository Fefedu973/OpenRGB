/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <QAbstractListModel>
#include <QApplication>
#include <QComboBox>
#include <QListView>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QStringList>
#include <functional>
#include <limits>

// A prefix (Entire Device/Zone), a lazy LED range and an optional Multiple tail.
// QComboBox's ordinary add/remove/clear calls remain valid for its small labels.
class LazyLEDListModel : public QAbstractListModel
{
public:
    explicit LazyLEDListModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex& parent = {}) const override
    { return parent.isValid() ? 0 : prefix.size()+count+suffix.size(); }
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override
    {
        if(!index.isValid() || index.row()<0 || index.row()>=rowCount() ||
           (role!=Qt::DisplayRole && role!=Qt::EditRole && role!=Qt::ToolTipRole)) return {};
        int row=index.row();
        if(row<prefix.size()) return prefix[row];
        row-=prefix.size();
        if(row<count) return label ? label(row) : QString();
        return suffix[row-count];
    }
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override
    {
        if(!index.isValid() || (role!=Qt::EditRole && role!=Qt::DisplayRole)) return false;
        int row=index.row();
        if(row>=0 && row<prefix.size()) prefix[row]=value.toString();
        else if(row>=prefix.size()+count && row<rowCount()) suffix[row-prefix.size()-count]=value.toString();
        else return false;
        emit dataChanged(index,index,{Qt::DisplayRole,Qt::EditRole}); return true;
    }
    bool insertRows(int row, int n, const QModelIndex& parent = {}) override
    {
        if(parent.isValid() || n<=0 || row!=rowCount() || n>std::numeric_limits<int>::max()-row) return false;
        beginInsertRows({},row,row+n-1);
        auto& items=count ? suffix : prefix;
        for(int i=0;i<n;++i) items.append(QString());
        endInsertRows(); return true;
    }
    bool removeRows(int row, int n, const QModelIndex& parent = {}) override
    {
        if(parent.isValid() || n<=0 || row<0 || row>rowCount()-n) return false;
        if(row==0 && n==rowCount())
        {
            beginResetModel(); prefix.clear(); suffix.clear(); count=0; label={}; endResetModel(); return true;
        }
        if(row+n!=rowCount() || (count && row<prefix.size()+count)) return false;
        beginRemoveRows({},row,row+n-1);
        auto& items=count ? suffix : prefix;
        for(int i=0;i<n;++i) items.removeLast();
        endRemoveRows(); return true;
    }
    void AppendRange(unsigned n, std::function<QString(unsigned)> provider)
    {
        if(!n || count || !suffix.empty() || n>unsigned(std::numeric_limits<int>::max()-prefix.size())) return;
        const int start=prefix.size();
        beginInsertRows({},start,start+int(n)-1); count=int(n); label=std::move(provider); endInsertRows();
    }
private:
    QStringList prefix, suffix;
    int count=0;
    std::function<QString(unsigned)> label;
};

// Qt's menu-like combo popup measures every row even with a lazy model.
// List-style popups measure only the visible rows. The proxy owns its freshly
// created base style, never QApplication's shared style instance.
class LazyLEDPopupStyle : public QProxyStyle
{
public:
    explicit LazyLEDPopupStyle(QStyle* base) : QProxyStyle(base) {}
    int styleHint(StyleHint hint, const QStyleOption* option = nullptr,
                  const QWidget* widget = nullptr, QStyleHintReturn* result = nullptr) const override
    {
        return hint == QStyle::SH_ComboBox_Popup ? 0 : QProxyStyle::styleHint(hint,option,widget,result);
    }
};

inline LazyLEDListModel* ConfigureLazyLEDComboBox(QComboBox* combo)
{
    auto* model = new LazyLEDListModel(combo);
    combo->setModel(model);
    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo->setMinimumContentsLength(24);
    auto* view = new QListView(combo);
    view->setUniformItemSizes(true);
    // Batched layout cannot reliably scroll directly to a yet-unlaid-out tail
    // row. Uniform SinglePass computes positions without materializing labels.
    view->setLayoutMode(QListView::SinglePass);
    combo->setView(view);
    auto* base = QStyleFactory::create(QApplication::style()->objectName());
    if(!base) base = QStyleFactory::create(QStringLiteral("Fusion"));
    auto* style = new LazyLEDPopupStyle(base);
    style->setParent(combo);
    combo->setStyle(style);
    view->setStyle(style);
    return model;
}
