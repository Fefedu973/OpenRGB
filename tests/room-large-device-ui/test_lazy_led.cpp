/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "LazyLEDListModel.h"
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListView>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

static void Check(bool condition,const char* message)
{ if(!condition) throw std::runtime_error(message); }
static quint64 PrivateBytes()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX info{};info.cb=sizeof(info);
    if(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&info),sizeof(info))) return info.PrivateUsage;
#endif
    return 0;
}
static void Configure(QComboBox& combo,LazyLEDListModel*& model)
{
    model=ConfigureLazyLEDComboBox(&combo);combo.resize(300,32);
}
static void ModelContract()
{
    LazyLEDListModel model;
    quint64 calls=0;
    QSignalSpy inserted(&model,&QAbstractItemModel::rowsInserted);
    Check(model.insertRows(0,1),"Prefix insert rejected");
    Check(model.setData(model.index(0),QString("Entire Device")),"Prefix label rejected");
    model.AppendRange(500000,[&](unsigned i){++calls;return QString("LED %1").arg(i);});
    Check(model.rowCount()==500001 && calls==0,"Range eagerly constructed labels");
    Check(inserted.count()==2,"Range was not one structural insertion");
    Check(model.data(model.index(0)).toString()=="Entire Device","Prefix offset wrong");
    Check(model.data(model.index(1)).toString()=="LED 0","First range offset wrong");
    Check(model.data(model.index(500000)).toString()=="LED 499999","Last range offset wrong");
    Check(calls==2,"Unexpected provider requests");
    Check(!model.data(model.index(1),Qt::DecorationRole).isValid() && calls==2,"Unused role generated labels");
    Check(!model.data(model.index(-1)).isValid() && !model.data(model.index(500001)).isValid(),"Invalid index accepted");
    Check(model.rowCount(model.index(0))==0,"List exposes child rows");
    Check(!model.setData(model.index(1),QString("mutation")),"Lazy range editable");
    Check(!model.removeRows(1,1) && !model.insertRows(1,1),"Lazy range was structurally mutated");
    Check(model.insertRows(model.rowCount(),1),"Multiple tail insert failed");
    Check(model.setData(model.index(500001),QString("Multiple (2)")),"Multiple label failed");
    Check(model.data(model.index(500001)).toString()=="Multiple (2)","Multiple position wrong");
    Check(model.removeRows(500001,1) && model.rowCount()==500001,"Multiple tail removal failed");
    model.AppendRange(10,[](unsigned){return QString("unexpected");});
    Check(model.rowCount()==500001,"Second range was appended");
    Check(model.removeRows(0,model.rowCount()) && model.rowCount()==0,"Clear failed");
    const auto previous=calls;
    model.AppendRange(1,[&](unsigned i){++calls;return QString("Single %1").arg(i);});
    Check(model.rowCount()==1 && model.data(model.index(0)).toString()=="Single 0","Single LED without prefix failed");
    Check(calls==previous+1,"Old provider survived reset");
    model.removeRows(0,model.rowCount());
    Check(model.insertRows(0,3),"Mode-specific colours insertion failed");
    for(int i=0;i<3;++i)Check(model.setData(model.index(i),QString("Color %1").arg(i)),"Mode label mutation failed");
    Check(model.data(model.index(2)).toString()=="Color 2","Mode-specific colours failed");
    Check(model.removeRows(2,1),"Small labels tail removal failed");
    model.removeRows(0,model.rowCount());
    model.insertRows(0,1);model.setData(model.index(0),QString("Entire Segment"));
    model.AppendRange(4,[](unsigned i){return QString("LED %1").arg(1200+i);});
    Check(model.data(model.index(1)).toString()=="LED 1200" && model.data(model.index(4)).toString()=="LED 1203","Zone/segment start offset lost");
}

static QJsonObject ComboBenchmark()
{
    // The real QComboBox configuration matches OpenRGBDevicePage, without its controller or hardware.
    QComboBox combo;LazyLEDListModel* model=nullptr;Configure(combo,model);
    quint64 calls=0;
    const auto before_memory=PrivateBytes();QElapsedTimer timer;timer.start();
    combo.addItem("Entire Device");
    model->AppendRange(500000,[&](unsigned i){++calls;return QString("Synthetic LED %1").arg(i);});
    const auto init_ns=timer.nsecsElapsed();const auto init_memory=PrivateBytes();const auto init_calls=calls;
    Check(combo.count()==500001 && init_calls<20,"Combo initialized every label");
    timer.restart();combo.setCurrentIndex(500000);const auto tail_ns=timer.nsecsElapsed();
    Check(combo.currentText()=="Synthetic LED 499999","Combo cannot select the final LED");
    combo.addItem("Multiple (123)");combo.setCurrentIndex(combo.count()-1);
    Check(combo.currentText()=="Multiple (123)","Combo Multiple tail failed");
    combo.setItemText(combo.count()-1,"Multiple (7)");
    Check(combo.currentText()=="Multiple (7)","Combo Multiple tail update failed");
    combo.removeItem(combo.count()-1);combo.setCurrentIndex(500000);
    Check(combo.currentText()=="Synthetic LED 499999","Removing tail changed LED identity");
    combo.show();QTest::qWait(30);
    const auto before_popup_calls=calls;const auto before_popup_memory=PrivateBytes();
    timer.restart();combo.showPopup();const auto popup_sync_ns=timer.nsecsElapsed();
    QTest::qWait(250);const auto popup_elapsed_ns=timer.nsecsElapsed();
    const auto popup_calls=calls-before_popup_calls;const auto after_popup_memory=PrivateBytes();
    const bool tail_visible=combo.view()->visualRect(model->index(500000)).intersects(combo.view()->viewport()->rect());
    QTest::keyClick(combo.view(),Qt::Key_Home);
    Check(combo.view()->currentIndex().row()==0,"Keyboard Home failed");
    QTest::keyClick(combo.view(),Qt::Key_End);
    Check(combo.view()->currentIndex().row()==500000,"Keyboard End failed");
    combo.hidePopup();combo.hide();
    QJsonObject result{
        {"qt_version",qVersion()},{"platform",QApplication::platformName()},
        {"led_rows",500000},{"prefix_rows",1},{"init_ms",init_ns/1000000.0},
        {"init_label_requests",double(init_calls)},{"tail_selection_ms",tail_ns/1000000.0},
        {"popup_sync_ms",popup_sync_ns/1000000.0},{"popup_observation_ms",popup_elapsed_ns/1000000.0},
        {"popup_label_requests",double(popup_calls)},{"tail_visible",tail_visible},
        {"view_current_row",combo.view()->currentIndex().row()},
        {"tail_rect_y",combo.view()->visualRect(model->index(500000)).y()},
        {"viewport_height",combo.view()->viewport()->height()},
        {"private_bytes_delta_initialization",double(init_memory)-double(before_memory)},
        {"private_bytes_delta_popup",double(after_popup_memory)-double(before_popup_memory)},
        {"scope","Synthetic QComboBox only, offscreen; not full OpenRGB performance"}
    };
    std::cout<<QJsonDocument(result).toJson(QJsonDocument::Compact).constData()<<std::endl;
    QFile output("measurements.json");if(output.open(QIODevice::WriteOnly))output.write(QJsonDocument(result).toJson());
    Check(tail_visible,"Selected tail item is outside popup viewport");
    Check(popup_calls<2000,"Popup generated an unbounded number of LED labels");
    combo.clear();Check(combo.count()==0,"QComboBox clear did not reset lazy model");
    combo.addItems({"Mode A","Mode B"});Check(combo.count()==2 && combo.itemText(1)=="Mode B","Small mode list failed after large range clear");
    return result;
}

static void StyleOwnership()
{
    QPointer<QStyle> shared=QApplication::style();
    {
        QComboBox first,second;LazyLEDListModel *a=nullptr,*b=nullptr;
        Configure(first,a);Configure(second,b);
        Check(first.style()!=shared && second.style()!=shared && first.style()!=second.style(),"Combo borrowed shared application style");
    }
    Check(!shared.isNull() && QApplication::style()==shared,"Destroying combo destroyed application style");
}

int main(int argc,char** argv)
{
    QApplication app(argc,argv);
    try
    {
        ModelContract();std::cout<<"PASS lazy500k, prefix, offsets, Multiple, clear and small modes\n";
        ComboBenchmark();std::cout<<"PASS real Qt combo, tail positioning, bounded label requests\n";
        StyleOwnership();std::cout<<"PASS independent proxy style ownership and widget destruction\n";
        return 0;
    }
    catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
