/****************************************************************************
**
** Copyright (C) 2012 Nokia Corporation and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/
**
** This file is part of the QtGui module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** GNU Lesser General Public License Usage
** This file may be used under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation and
** appearing in the file LICENSE.LGPL included in the packaging of this
** file. Please review the following information to ensure the GNU Lesser
** General Public License version 2.1 requirements will be met:
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights. These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU General
** Public License version 3.0 as published by the Free Software Foundation
** and appearing in the file LICENSE.GPL included in the packaging of this
** file. Please review the following information to ensure the GNU General
** Public License version 3.0 requirements will be met:
** http://www.gnu.org/copyleft/gpl.html.
**
** Other Usage
** Alternatively, this file may be used in accordance with the terms and
** conditions contained in a signed written agreement between you and Nokia.
**
**
**
**
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qaccessiblewidget.h"

#ifndef QT_NO_ACCESSIBILITY

#include "qaction.h"
#include "qapplication.h"
#include "qgroupbox.h"
#include "qlabel.h"
#include "qtooltip.h"
#include "qwhatsthis.h"
#include "qwidget.h"
#include "qdebug.h"
#include <qmath.h>
#include <QRubberBand>
#include <QFocusFrame>
#include <QMenu>

QT_BEGIN_NAMESPACE

static QList<QWidget*> childWidgets(const QWidget *widget)
{
    QList<QObject*> list = widget->children();
    QList<QWidget*> widgets;
    for (int i = 0; i < list.size(); ++i) {
        QWidget *w = qobject_cast<QWidget *>(list.at(i));
        if (w && !w->isWindow() 
            && !qobject_cast<QFocusFrame*>(w)
#if !defined(QT_NO_MENU)
            && !qobject_cast<QMenu*>(w)
#endif
            && w->objectName() != QLatin1String("qt_rubberband"))
            widgets.append(w);
    }
    return widgets;
}

static QString buddyString(const QWidget *widget)
{
    if (!widget)
        return QString();
    QWidget *parent = widget->parentWidget();
    if (!parent)
        return QString();
#ifndef QT_NO_SHORTCUT
    QObjectList ol = parent->children();
    for (int i = 0; i < ol.size(); ++i) {
        QLabel *label = qobject_cast<QLabel*>(ol.at(i));
        if (label && label->buddy() == widget)
            return label->text();
    }
#endif

#ifndef QT_NO_GROUPBOX
    QGroupBox *groupbox = qobject_cast<QGroupBox*>(parent);
    if (groupbox)
        return groupbox->title();
#endif

    return QString();
}

/* This function will return the offset of the '&' in the text that would be
   preceding the accelerator character.
   If this text does not have an accelerator, -1 will be returned. */
static int qt_accAmpIndex(const QString &text)
{
#ifndef QT_NO_SHORTCUT
    if (text.isEmpty())
        return -1;

    int fa = 0;
    QChar ac;
    while ((fa = text.indexOf(QLatin1Char('&'), fa)) != -1) {
        ++fa;
        if (fa < text.length()) {
            // ignore "&&"
            if (text.at(fa) == QLatin1Char('&')) {

                ++fa;
                continue;
            } else {
                return fa - 1;
                break;
            }
        }
    }

    return -1;
#else
    Q_UNUSED(text);
    return -1;
#endif
}

QString Q_WIDGETS_EXPORT qt_accStripAmp(const QString &text)
{
    QString newText(text);
    int ampIndex = qt_accAmpIndex(newText);
    if (ampIndex != -1)
        newText.remove(ampIndex, 1);

    return newText.replace(QLatin1String("&&"), QLatin1String("&"));
}

QString Q_WIDGETS_EXPORT qt_accHotKey(const QString &text)
{
    int ampIndex = qt_accAmpIndex(text);
    if (ampIndex != -1)
        return (QString)QKeySequence(Qt::ALT) + text.at(ampIndex + 1);

    return QString();
}

class QAccessibleWidgetPrivate
{
public:
    QAccessibleWidgetPrivate()
        :role(QAccessible::Client)
    {}

    QAccessible::Role role;
    QString name;
    QString description;
    QString value;
    QString help;
    QString accelerator;
    QStringList primarySignals;
    const QAccessibleInterface *asking;
};

/*!
    \class QAccessibleWidget
    \brief The QAccessibleWidget class implements the QAccessibleInterface for QWidgets.
    \internal

    \ingroup accessibility
    \inmodule QtWidgets

    This class is part of \l {Accessibility for QWidget Applications}.

    This class is convenient to use as a base class for custom
    implementations of QAccessibleInterfaces that provide information
    about widget objects.

    The class provides functions to retrieve the parentObject() (the
    widget's parent widget), and the associated widget(). Controlling
    signals can be added with addControllingSignal(), and setters are
    provided for various aspects of the interface implementation, for
    example setValue(), setDescription(), setAccelerator(), and
    setHelp().

    \sa QAccessible, QAccessibleObject
*/

/*!
    Creates a QAccessibleWidget object for widget \a w.
    \a role and \a name are optional parameters that set the object's
    role and name properties.
*/
QAccessibleWidget::QAccessibleWidget(QWidget *w, QAccessible::Role role, const QString &name)
: QAccessibleObject(w)
{
    Q_ASSERT(widget());
    d = new QAccessibleWidgetPrivate();
    d->role = role;
    d->name = name;
    d->asking = 0;
}

/*! \reimp */
QWindow *QAccessibleWidget::window() const
{
    return widget()->windowHandle();
}

/*!
    Destroys this object.
*/
QAccessibleWidget::~QAccessibleWidget()
{
    delete d;
}

/*!
    Returns the associated widget.
*/
QWidget *QAccessibleWidget::widget() const
{
    return qobject_cast<QWidget*>(object());
}

/*!
    Returns the associated widget's parent object, which is either the
    parent widget, or qApp for top-level widgets.
*/
QObject *QAccessibleWidget::parentObject() const
{
    QObject *parent = object()->parent();
    if (!parent)
        parent = qApp;
    return parent;
}

/*! \reimp */
QRect QAccessibleWidget::rect() const
{
    QWidget *w = widget();
    if (!w->isVisible())
        return QRect();
    QPoint wpos = w->mapToGlobal(QPoint(0, 0));

    return QRect(wpos.x(), wpos.y(), w->width(), w->height());
}

QT_BEGIN_INCLUDE_NAMESPACE
#include <private/qobject_p.h>
QT_END_INCLUDE_NAMESPACE

class QACConnectionObject : public QObject
{
    Q_DECLARE_PRIVATE(QObject)
public:
    inline bool isSender(const QObject *receiver, const char *signal) const
    { return d_func()->isSender(receiver, signal); }
    inline QObjectList receiverList(const char *signal) const
    { return d_func()->receiverList(signal); }
    inline QObjectList senderList() const
    { return d_func()->senderList(); }
};

/*!
    Registers \a signal as a controlling signal.

    An object is a Controller to any other object connected to a
    controlling signal.
*/
void QAccessibleWidget::addControllingSignal(const QString &signal)
{
    QByteArray s = QMetaObject::normalizedSignature(signal.toAscii());
    if (object()->metaObject()->indexOfSignal(s) < 0)
        qWarning("Signal %s unknown in %s", s.constData(), object()->metaObject()->className());
    d->primarySignals << QLatin1String(s);
}

/*!
    Sets the value of this interface implementation to \a value.

    The default implementation of text() returns the set value for
    the Value text.

    Note that the object wrapped by this interface is not modified.
*/
void QAccessibleWidget::setValue(const QString &value)
{
    d->value = value;
}

/*!
    Sets the description of this interface implementation to \a desc.

    The default implementation of text() returns the set value for
    the Description text.

    Note that the object wrapped by this interface is not modified.
*/
void QAccessibleWidget::setDescription(const QString &desc)
{
    d->description = desc;
}

/*!
    Sets the help of this interface implementation to \a help.

    The default implementation of text() returns the set value for
    the Help text.

    Note that the object wrapped by this interface is not modified.
*/
void QAccessibleWidget::setHelp(const QString &help)
{
    d->help = help;
}

/*!
    Sets the accelerator of this interface implementation to \a accel.

    The default implementation of text() returns the set value for
    the Accelerator text.

    Note that the object wrapped by this interface is not modified.
*/
void QAccessibleWidget::setAccelerator(const QString &accel)
{
    d->accelerator = accel;
}

static inline bool isAncestor(const QObject *obj, const QObject *child)
{
    while (child) {
        if (child == obj)
            return true;
        child = child->parent();
    }
    return false;
}

/*! \reimp */
QVector<QPair<QAccessibleInterface*, QAccessible::Relation> >
QAccessibleWidget::relations(QAccessible::Relation match /*= QAccessible::AllRelations*/) const
{
    QVector<QPair<QAccessibleInterface*, QAccessible::Relation> > rels;
    if (match & QAccessible::Label) {
        const QAccessible::Relation rel = QAccessible::Label;
        if (QWidget *parent = widget()->parentWidget()) {
#ifndef QT_NO_SHORTCUT
            // first check for all siblings that are labels to us
            // ideally we would go through all objects and check, but that
            // will be too expensive
            const QList<QWidget*> kids = childWidgets(parent);
            for (int i = 0; i < kids.count(); ++i) {
                if (QLabel *labelSibling = qobject_cast<QLabel*>(kids.at(i))) {
                    if (labelSibling->buddy() == widget()) {
                        QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(labelSibling);
                        rels.append(qMakePair(iface, rel));
                    }
                }
            }
#endif
#ifndef QT_NO_GROUPBOX
            QGroupBox *groupbox = qobject_cast<QGroupBox*>(parent);
            if (groupbox && !groupbox->title().isEmpty()) {
                QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(groupbox);
                rels.append(qMakePair(iface, rel));
            }
#endif
        }
    }

    if (match & QAccessible::Controller) {
        const QAccessible::Relation rel = QAccessible::Controller;
        QACConnectionObject *connectionObject = (QACConnectionObject*)object();
        const QObjectList senderList = connectionObject->senderList();
        for (int s = 0; s < senderList.count(); ++s) {
            QObject *sender = senderList.at(s);
            if (sender->isWidgetType() && sender != object()) {
                QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(sender);
                QACConnectionObject *connectionSender = (QACConnectionObject*)sender;
                QStringList senderPrimarySignals = static_cast<QAccessibleWidget*>(iface)->d->primarySignals;
                for (int sig = 0; sig < senderPrimarySignals.count(); ++sig) {
                    const QByteArray strSignal = senderPrimarySignals.at(sig).toAscii();
                    if (connectionSender->isSender(object(), strSignal.constData()))
                        rels.append(qMakePair(iface, rel));
                }
            }
        }
    }

    if (match & QAccessible::Controlled) {
        QObjectList allReceivers;
        QACConnectionObject *connectionObject = (QACConnectionObject*)object();
        for (int sig = 0; sig < d->primarySignals.count(); ++sig) {
            const QObjectList receivers = connectionObject->receiverList(d->primarySignals.at(sig).toAscii());
            allReceivers += receivers;
        }

        allReceivers.removeAll(object());  //### The object might connect to itself internally

        for (int i = 0; i < allReceivers.count(); ++i) {
            const QAccessible::Relation rel = QAccessible::Controlled;
            QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(allReceivers.at(i));
            if (iface)
                rels.append(qMakePair(iface, rel));
        }
    }

    return rels;
}

/*! \reimp */
QAccessibleInterface *QAccessibleWidget::parent() const
{
    QObject *parentWidget= widget()->parentWidget();
    if (!parentWidget)
        parentWidget = qApp;
    return QAccessible::queryAccessibleInterface(parentWidget);
}

/*! \reimp */
QAccessibleInterface *QAccessibleWidget::child(int index) const
{
    QWidgetList childList = childWidgets(widget());
    if (index >= 0 && index < childList.size())
        return QAccessible::queryAccessibleInterface(childList.at(index));
    return 0;
}

/*! \reimp */
QAccessibleInterface *QAccessibleWidget::focusChild() const
{
    if (widget()->hasFocus())
        return QAccessible::queryAccessibleInterface(object());

    QWidget *fw = widget()->focusWidget();
    if (!fw)
        return 0;

    if (isAncestor(widget(), fw) || fw == widget())
        return QAccessible::queryAccessibleInterface(fw);
    return 0;
}

/*! \reimp */
int QAccessibleWidget::childCount() const
{
    QWidgetList cl = childWidgets(widget());
    return cl.size();
}

/*! \reimp */
int QAccessibleWidget::indexOfChild(const QAccessibleInterface *child) const
{
    QWidgetList cl = childWidgets(widget());
    return cl.indexOf(qobject_cast<QWidget *>(child->object()));
}

// from qwidget.cpp
extern QString qt_setWindowTitle_helperHelper(const QString &, const QWidget*);

/*! \reimp */
QString QAccessibleWidget::text(QAccessible::Text t) const
{
    QString str;

    switch (t) {
    case QAccessible::Name:
        if (!d->name.isEmpty()) {
            str = d->name;
        } else if (!widget()->accessibleName().isEmpty()) {
            str = widget()->accessibleName();
        } else if (widget()->isWindow()) {
            if (widget()->isMinimized())
                str = qt_setWindowTitle_helperHelper(widget()->windowIconText(), widget());
            else
                str = qt_setWindowTitle_helperHelper(widget()->windowTitle(), widget());
        } else {
            str = qt_accStripAmp(buddyString(widget()));
        }
        break;
    case QAccessible::Description:
        if (!d->description.isEmpty())
            str = d->description;
        else if (!widget()->accessibleDescription().isEmpty())
            str = widget()->accessibleDescription();
#ifndef QT_NO_TOOLTIP
        else
            str = widget()->toolTip();
#endif
        break;
    case QAccessible::Help:
        if (!d->help.isEmpty())
            str = d->help;
#ifndef QT_NO_WHATSTHIS
        else
            str = widget()->whatsThis();
#endif
        break;
    case QAccessible::Accelerator:
        if (!d->accelerator.isEmpty())
            str = d->accelerator;
        else
            str = qt_accHotKey(buddyString(widget()));
        break;
    case QAccessible::Value:
        str = d->value;
        break;
    default:
        break;
    }
    return str;
}

/*! \reimp */
QStringList QAccessibleWidget::actionNames() const
{
    QStringList names;
    if (widget()->isEnabled()) {
        if (widget()->focusPolicy() != Qt::NoFocus)
            names << setFocusAction();
    }
    return names;
}

/*! \reimp */
void QAccessibleWidget::doAction(const QString &actionName)
{
    if (!widget()->isEnabled())
        return;

    if (actionName == setFocusAction()) {
        if (widget()->isWindow())
            widget()->activateWindow();
        widget()->setFocus();
    }
}

/*! \reimp */
QStringList QAccessibleWidget::keyBindingsForAction(const QString & /* actionName */) const
{
    return QStringList();
}

/*! \reimp */
QAccessible::Role QAccessibleWidget::role() const
{
    return d->role;
}

/*! \reimp */
QAccessible::State QAccessibleWidget::state() const
{
    QAccessible::State state;

    QWidget *w = widget();
    if (w->testAttribute(Qt::WA_WState_Visible) == false)
        state.invisible = true;
    if (w->focusPolicy() != Qt::NoFocus)
        state.focusable = true;
    if (w->hasFocus())
        state.focused = true;
    if (!w->isEnabled())
        state.disabled = true;
    if (w->isWindow()) {
        if (w->windowFlags() & Qt::WindowSystemMenuHint)
            state.movable = true;
        if (w->minimumSize() != w->maximumSize())
            state.sizeable = true;
        if (w->isActiveWindow())
            state.active = true;
    }

    return state;
}

/*! \reimp */
QColor QAccessibleWidget::foregroundColor() const
{
    return widget()->palette().color(widget()->foregroundRole());
}

/*! \reimp */
QColor QAccessibleWidget::backgroundColor() const
{
    return widget()->palette().color(widget()->backgroundRole());
}

/*! \reimp */
void *QAccessibleWidget::interface_cast(QAccessible::InterfaceType t)
{
    if (t == QAccessible::ActionInterface)
       return static_cast<QAccessibleActionInterface*>(this);
    return 0;
}

QT_END_NAMESPACE

#endif //QT_NO_ACCESSIBILITY
