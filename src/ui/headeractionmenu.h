#pragma once

#include <QMenu>

class QAction;

/** 歌曲列表页顶部「更多」按钮使用的统一菜单。 */
class HeaderActionMenu : public QMenu
{
    Q_OBJECT

public:
    explicit HeaderActionMenu(QWidget *parent = nullptr);

    QAction *addHeaderAction(const QString &text, const char *iconName, bool enabled = true);
};
