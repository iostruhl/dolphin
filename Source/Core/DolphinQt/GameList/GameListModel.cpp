// Copyright 2015 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "DolphinQt/GameList/GameListModel.h"

#include <QPixmap>

#include "Core/ConfigManager.h"

#include "DiscIO/Enums.h"

#include "DolphinQt/QtUtils/ImageConverter.h"
#include "DolphinQt/Resources.h"
#include "DolphinQt/Settings.h"

#include "UICommon/UICommon.h"

const QSize GAMECUBE_BANNER_SIZE(96, 32);

GameListModel::GameListModel(QObject* parent) : QAbstractTableModel(parent)
{
  connect(&m_tracker, &GameTracker::GameLoaded, this, &GameListModel::AddGame);
  connect(&m_tracker, &GameTracker::GameUpdated, this, &GameListModel::UpdateGame);
  connect(&m_tracker, &GameTracker::GameRemoved, this, &GameListModel::RemoveGame);
  connect(&Settings::Instance(), &Settings::PathAdded, &m_tracker, &GameTracker::AddDirectory);
  connect(&Settings::Instance(), &Settings::PathRemoved, &m_tracker, &GameTracker::RemoveDirectory);
  connect(&Settings::Instance(), &Settings::GameListRefreshRequested, &m_tracker,
          &GameTracker::RefreshAll);
  connect(&Settings::Instance(), &Settings::TitleDBReloadRequested, this,
          [this] { m_title_database = Core::TitleDatabase(); });

  for (const QString& dir : Settings::Instance().GetPaths())
    m_tracker.AddDirectory(dir);

  m_tracker.Start();

  connect(&Settings::Instance(), &Settings::ThemeChanged, [this] {
    // Tell the view to repaint. The signal 'dataChanged' also seems like it would work here, but
    // unfortunately it won't cause a repaint until the view is focused.
    emit layoutAboutToBeChanged();
    emit layoutChanged();
  });
}

QVariant GameListModel::data(const QModelIndex& index, int role) const
{
  if (!index.isValid())
    return QVariant();

  const UICommon::GameFile& game = *m_games[index.row()];

  switch (index.column())
  {
  case COL_PLATFORM:
    if (role == Qt::DecorationRole)
      return Resources::GetPlatform(static_cast<int>(game.GetPlatform()));
    if (role == Qt::InitialSortOrderRole)
      return static_cast<int>(game.GetPlatform());
    break;
  case COL_COUNTRY:
    if (role == Qt::DecorationRole)
      return Resources::GetCountry(static_cast<int>(game.GetCountry()));
    if (role == Qt::InitialSortOrderRole)
      return static_cast<int>(game.GetCountry());
    break;
  case COL_BANNER:
    if (role == Qt::DecorationRole)
    {
      // GameCube banners are 96x32, but Wii banners are 192x64.
      QPixmap banner = ToQPixmap(game.GetBannerImage());
      if (banner.isNull())
        banner = Resources::GetMisc(Resources::BANNER_MISSING);

      banner.setDevicePixelRatio(
          std::max(static_cast<qreal>(banner.width()) / GAMECUBE_BANNER_SIZE.width(),
                   static_cast<qreal>(banner.height()) / GAMECUBE_BANNER_SIZE.height()));

      return banner;
    }
    break;
  case COL_TITLE:
    if (role == Qt::DisplayRole || role == Qt::InitialSortOrderRole)
    {
      QString name = QString::fromStdString(game.GetName(m_title_database));
      const int disc_nr = game.GetDiscNumber() + 1;
      if (disc_nr > 1)
      {
        if (!name.contains(QRegExp(QStringLiteral("disc ?%1").arg(disc_nr), Qt::CaseInsensitive)))
        {
          name.append(tr(" (Disc %1)").arg(disc_nr));
        }
      }
      return name;
    }
    break;
  case COL_ID:
    if (role == Qt::DisplayRole || role == Qt::InitialSortOrderRole)
      return QString::fromStdString(game.GetGameID());
    break;
  case COL_DESCRIPTION:
    if (role == Qt::DisplayRole || role == Qt::InitialSortOrderRole)
      return QString::fromStdString(game.GetDescription());
    break;
  case COL_MAKER:
    if (role == Qt::DisplayRole || role == Qt::InitialSortOrderRole)
      return QString::fromStdString(game.GetMaker());
    break;
  case COL_FILE_NAME:
    if (role == Qt::DisplayRole || role == Qt::InitialSortOrderRole)
      return QString::fromStdString(game.GetFileName());
    break;
  case COL_SIZE:
    if (role == Qt::DisplayRole)
      return QString::fromStdString(UICommon::FormatSize(game.GetFileSize()));
    if (role == Qt::InitialSortOrderRole)
      return static_cast<quint64>(game.GetFileSize());
    break;
  }

  return QVariant();
}

QVariant GameListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
  if (orientation == Qt::Vertical || role != Qt::DisplayRole)
    return QVariant();

  switch (section)
  {
  case COL_TITLE:
    return tr("Title");
  case COL_ID:
    return tr("ID");
  case COL_BANNER:
    return tr("Banner");
  case COL_DESCRIPTION:
    return tr("Description");
  case COL_MAKER:
    return tr("Maker");
  case COL_FILE_NAME:
    return tr("File Name");
  case COL_SIZE:
    return tr("Size");
  }
  return QVariant();
}

int GameListModel::rowCount(const QModelIndex& parent) const
{
  if (parent.isValid())
    return 0;
  return m_games.size();
}

int GameListModel::columnCount(const QModelIndex& parent) const
{
  return NUM_COLS;
}

bool GameListModel::ShouldDisplayGameListItem(int index) const
{
  const UICommon::GameFile& game = *m_games[index];

  if (!m_term.isEmpty() &&
      !QString::fromStdString(game.GetName(m_title_database)).contains(m_term, Qt::CaseInsensitive))
  {
    return false;
  }

  const bool show_platform = [&game] {
    switch (game.GetPlatform())
    {
    case DiscIO::Platform::GameCubeDisc:
      return SConfig::GetInstance().m_ListGC;
    case DiscIO::Platform::WiiDisc:
      return SConfig::GetInstance().m_ListWii;
    case DiscIO::Platform::WiiWAD:
      return SConfig::GetInstance().m_ListWad;
    case DiscIO::Platform::ELFOrDOL:
      return SConfig::GetInstance().m_ListElfDol;
    default:
      return false;
    }
  }();

  if (!show_platform)
    return false;

  switch (game.GetCountry())
  {
  case DiscIO::Country::Australia:
    return SConfig::GetInstance().m_ListAustralia;
  case DiscIO::Country::Europe:
    return SConfig::GetInstance().m_ListPal;
  case DiscIO::Country::France:
    return SConfig::GetInstance().m_ListFrance;
  case DiscIO::Country::Germany:
    return SConfig::GetInstance().m_ListGermany;
  case DiscIO::Country::Italy:
    return SConfig::GetInstance().m_ListItaly;
  case DiscIO::Country::Japan:
    return SConfig::GetInstance().m_ListJap;
  case DiscIO::Country::Korea:
    return SConfig::GetInstance().m_ListKorea;
  case DiscIO::Country::Netherlands:
    return SConfig::GetInstance().m_ListNetherlands;
  case DiscIO::Country::Russia:
    return SConfig::GetInstance().m_ListRussia;
  case DiscIO::Country::Spain:
    return SConfig::GetInstance().m_ListSpain;
  case DiscIO::Country::Taiwan:
    return SConfig::GetInstance().m_ListTaiwan;
  case DiscIO::Country::USA:
    return SConfig::GetInstance().m_ListUsa;
  case DiscIO::Country::World:
    return SConfig::GetInstance().m_ListWorld;
  case DiscIO::Country::Unknown:
  default:
    return SConfig::GetInstance().m_ListUnknown;
  }
}

std::shared_ptr<const UICommon::GameFile> GameListModel::GetGameFile(int index) const
{
  return m_games[index];
}

void GameListModel::AddGame(const std::shared_ptr<const UICommon::GameFile>& game)
{
  beginInsertRows(QModelIndex(), m_games.size(), m_games.size());
  m_games.push_back(game);
  endInsertRows();
}

void GameListModel::UpdateGame(const std::shared_ptr<const UICommon::GameFile>& game)
{
  int index = FindGame(game->GetFilePath());
  if (index < 0)
  {
    AddGame(game);
  }
  else
  {
    m_games[index] = game;
    emit dataChanged(createIndex(index, 0), createIndex(index + 1, columnCount(QModelIndex())));
  }
}

void GameListModel::RemoveGame(const std::string& path)
{
  int entry = FindGame(path);
  if (entry < 0)
    return;

  beginRemoveRows(QModelIndex(), entry, entry);
  m_games.removeAt(entry);
  endRemoveRows();
}

int GameListModel::FindGame(const std::string& path) const
{
  for (int i = 0; i < m_games.size(); i++)
  {
    if (m_games[i]->GetFilePath() == path)
      return i;
  }
  return -1;
}

void GameListModel::SetSearchTerm(const QString& term)
{
  m_term = term;
}
