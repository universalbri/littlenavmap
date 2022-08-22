/*****************************************************************************
* Copyright 2015-2020 Alexander Barthel alex@littlenavmap.org
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*****************************************************************************/

#include "routestring/routestringdialog.h"

#include "routestring/routestringwriter.h"
#include "routestring/routestringreader.h"
#include "navapp.h"
#include "settings/settings.h"
#include "query/procedurequery.h"
#include "query/airportquery.h"
#include "route/routecontroller.h"
#include "fs/pln/flightplan.h"
#include "gui/helphandler.h"
#include "gui/widgetstate.h"
#include "common/constants.h"
#include "common/unit.h"
#include "atools.h"

#include "ui_routestringdialog.h"

#include <QClipboard>
#include <QAction>
#include <QMenu>
#include <QButtonGroup>

const static int TEXT_CHANGE_DELAY_MS = 500;

using atools::gui::HelpHandler;
namespace apln = atools::fs::pln;

RouteStringDialog::RouteStringDialog(QWidget *parent, const QString& routeStringParam)
  : QDialog(parent), ui(new Ui::RouteStringDialog), routeString(routeStringParam)
{
  setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
  setWindowModality(Qt::ApplicationModal);

  controller = NavApp::getRouteController();

  ui->setupUi(this);

  // Styles cascade to children and mess up UI themes on linux - even if widget is selected by name
#ifndef Q_OS_LINUX
  // Make the splitter handle better visible
  ui->splitterRouteString->setStyleSheet(QString("QSplitter::handle { "
                                                 "background: %1;"
                                                 "image: url(:/littlenavmap/resources/icons/splitterhandvert.png); "
                                                 "}").
                                         arg(QApplication::palette().color(QPalette::Window).darker(120).name()));
#endif

  // Disallow collapsing of the upper view
  ui->splitterRouteString->setCollapsible(0, false);
  if(ui->splitterRouteString->handle(1) != nullptr)
  {
    ui->splitterRouteString->handle(1)->
    setToolTip(tr("Resize upper and lower part or open and close lower message area of the dialog."));
    ui->splitterRouteString->handle(1)->setStatusTip(ui->splitterRouteString->handle(1)->toolTip());
  }

  QFont fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
#if defined(Q_OS_MACOS)
  fixedFont.setPointSizeF(fixedFont.pointSizeF() * 1.2);
#endif
  ui->plainTextEditRouteString->setFont(fixedFont);
  ui->plainTextEditRouteString->setWordWrapMode(QTextOption::WrapAnywhere);

  ui->buttonBoxRouteString->button(QDialogButtonBox::Ok)->setText(tr("Create Flight &Plan"));

  flightplan = new apln::Flightplan;
  routeStringWriter = new RouteStringWriter();
  routeStringReader = new RouteStringReader(controller->getFlightplanEntryBuilder());

  // Build options dropdown menu ====================================================
  // Add tear off menu =======
  ui->toolButtonRouteStringOptions->setMenu(new QMenu(ui->toolButtonRouteStringOptions));
  QMenu *buttonMenu = ui->toolButtonRouteStringOptions->menu();
  buttonMenu->setToolTipsVisible(true);
  buttonMenu->setTearOffEnabled(true);

  // Writing to string ===========================================
  QAction *action;
  action = new QAction(tr("Write departure and destination airport"), buttonMenu);
  action->setObjectName("actionDepartDest");
  action->setToolTip(tr("Omit departure and destination airport ICAO code.\n"
                        "Note that the resulting description cannot be read into a flight plan."));
  action->setCheckable(true);
  action->setData(static_cast<int>(rs::START_AND_DEST));
  buttonMenu->addAction(action);

  action = new QAction(tr("Write DCT (direct) instructions"), buttonMenu);
  action->setObjectName("actionDct");
  action->setToolTip(tr("Fill direct connections between waypoints with a \"DCT\""));
  action->setCheckable(true);
  action->setData(static_cast<int>(rs::DCT));
  buttonMenu->addAction(action);

  action = new QAction(tr("Write cruise speed and altitude instruction"), buttonMenu);
  action->setObjectName("actionSpeedAlt");
  action->setToolTip(tr("Add cruise speed and altitude to description.\n"
                        "Speed is ignored in favor to currently loaded aircraft performance\n"
                        "when reading a description into a flight plan."));
  action->setCheckable(true);
  action->setData(static_cast<int>(rs::ALT_AND_SPEED));
  buttonMenu->addAction(action);

  action = new QAction(tr("Write Waypoints instead of Airways"), buttonMenu);
  action->setObjectName("actionWaypoints");
  action->setToolTip(tr("Ignore airways and add all waypoints instead"));
  action->setCheckable(true);
  action->setData(static_cast<int>(rs::NO_AIRWAYS));
  buttonMenu->addAction(action);

  action = new QAction(tr("Write Alternates"), buttonMenu);
  action->setObjectName("actionAlternates");
  action->setToolTip(tr("Add the ICAO code for all alternate airports to the end of the description"));
  action->setCheckable(true);
  action->setData(static_cast<int>(rs::ALTERNATES));
  buttonMenu->addAction(action);

  buttonMenu->addSeparator();

  // SID/STAR group ===========================================
  procActionGroup = new QActionGroup(buttonMenu);
  if(NavApp::hasSidStarInDatabase())
  {
    action = new QAction(tr("Write SID and STAR"), buttonMenu);
    action->setObjectName("actionSidStar");
    action->setToolTip(tr("Write SID, STAR and the respective transitions to the description"));
    action->setCheckable(true);
    action->setData(static_cast<int>(rs::SID_STAR));
    buttonMenu->addAction(action);
    procActionGroup->addAction(action);
  }

  action = new QAction(tr("Write generic SID and STAR"), buttonMenu);
  action->setObjectName("actionGenericSidStar");
  action->setToolTip(tr("Add \"SID\" and \"STAR\" words only instead of the real procedure names"));
  action->setCheckable(true);
  action->setData(static_cast<int>(rs::SID_STAR_GENERIC));
  buttonMenu->addAction(action);
  procActionGroup->addAction(action);

  action = new QAction(tr("Write no SID and STAR"), buttonMenu);
  action->setObjectName("actionNoSidStar");
  action->setToolTip(tr("Add neither SID nor STAR to the description"));
  action->setCheckable(true);
  action->setData(static_cast<int>(rs::SID_STAR_NONE));
  buttonMenu->addAction(action);
  procActionGroup->addAction(action);

  buttonMenu->addSeparator();

  action = new QAction(tr("Write STAR and transition reversed"), buttonMenu);
  action->setObjectName("actionReversedStar");
  action->setToolTip(tr("Write \"TRANS.STAR\" instead of \"STAR.TRANS\""));
  action->setCheckable(true);
  action->setData(static_cast<int>(rs::STAR_REV_TRANSITION));
  buttonMenu->addAction(action);

  action = new QAction(tr("Write SID/STAR and transition space separated"), buttonMenu);
  action->setObjectName("actionSpaceSidStar");
  action->setToolTip(tr("Use a space to separate SID, STAR and transition"));
  action->setCheckable(true);
  action->setData(static_cast<int>(rs::SID_STAR_SPACE));
  buttonMenu->addAction(action);

  buttonMenu->addSeparator();

  // Reading from string ===========================================
  action = new QAction(tr("Read trailing Airports as Alternates"), buttonMenu);
  action->setObjectName("actionTrailingAlternates");
  action->setToolTip(tr("A list of airports at the end of the description will be read as alternate "
                        "airports when reading if checked.\n"
                        "Otherwise airports are added as waypoints."));
  action->setCheckable(true);
  action->setData(static_cast<int>(rs::READ_ALTERNATES));
  buttonMenu->addAction(action);

  action = new QAction(tr("Read first and last item as Navaid"), buttonMenu);
  action->setObjectName("actionNavaid");
  action->setToolTip(tr("Does not expect the first and last string item to be an airport ICAO ident if checked"));
  action->setCheckable(true);
  action->setData(static_cast<int>(rs::READ_NO_AIRPORTS));
  buttonMenu->addAction(action);

  action = new QAction(tr("Read: Match coordinates to Waypoints"), buttonMenu);
  action->setObjectName("actionMatchCoords");
  action->setToolTip(tr("Coordinates will be converted to navaids if nearby"));
  action->setCheckable(true);
  action->setData(static_cast<int>(rs::READ_MATCH_WAYPOINTS));
  buttonMenu->addAction(action);

  connect(ui->pushButtonRouteStringFromClipboard, &QPushButton::clicked, this, &RouteStringDialog::fromClipboardClicked);
  connect(ui->pushButtonRouteStringToClipboard, &QPushButton::clicked, this, &RouteStringDialog::toClipboardClicked);

  connect(ui->plainTextEditRouteString, &QPlainTextEdit::textChanged, this, &RouteStringDialog::updateButtonState);
  connect(ui->plainTextEditRouteString, &QPlainTextEdit::textChanged, this, &RouteStringDialog::textChanged);

  connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, &RouteStringDialog::updateButtonState);

  connect(ui->buttonBoxRouteString, &QDialogButtonBox::clicked, this, &RouteStringDialog::buttonBoxClicked);

  connect(ui->toolButtonRouteStringOptions->menu(), &QMenu::triggered, this, &RouteStringDialog::toolButtonOptionTriggered);

  connect(ui->pushButtonRouteStringUpdate, &QPushButton::clicked, this, &RouteStringDialog::updateButtonClicked);

  connect(&textUpdateTimer, &QTimer::timeout, this, &RouteStringDialog::textChangedDelayed);
  textUpdateTimer.setSingleShot(true);
}

RouteStringDialog::~RouteStringDialog()
{
  textUpdateTimer.stop();
  delete routeStringWriter;
  delete routeStringReader;
  delete procActionGroup;
  delete ui;
  delete flightplan;
}

void RouteStringDialog::updateButtonClicked()
{
  ui->plainTextEditRouteString->setPlainText(routeStringWriter->createStringForRoute(NavApp::getRouteConst(),
                                                                                     NavApp::getRouteCruiseSpeedKts(),
                                                                                     options));
  textChangedDelayed();
}

void RouteStringDialog::toolButtonOptionTriggered(QAction *action)
{
  Q_UNUSED(action)

  if(updatingActions)
    return;

  qDebug() << Q_FUNC_INFO << action->objectName() << action->data();

  // Copy menu state for options bitfield
  for(const QAction *act : ui->toolButtonRouteStringOptions->menu()->actions())
  {
    rs::RouteStringOptions opts(act->data().toInt());
    if(act->isChecked())
      options |= opts;
    else
      options &= ~opts;
  }
  textChangedDelayed();
}

const atools::fs::pln::Flightplan& RouteStringDialog::getFlightplan() const
{
  return *flightplan;
}

void RouteStringDialog::saveState()
{
  atools::gui::WidgetState(lnm::ROUTE_STRING_DIALOG_SPLITTER).save(
    {this, ui->splitterRouteString, ui->comboBoxRouteStringFlightplanType});
  atools::settings::Settings::instance().setValue(lnm::ROUTE_STRING_DIALOG_OPTIONS, static_cast<int>(options));
}

void RouteStringDialog::restoreState()
{
  atools::gui::WidgetState(lnm::ROUTE_STRING_DIALOG_SPLITTER).restore(
    {this, ui->splitterRouteString, ui->comboBoxRouteStringFlightplanType});
  ui->splitterRouteString->setHandleWidth(6);
  options = getOptionsFromSettings();
  updateButtonState();

  if(routeString.isEmpty())
    ui->plainTextEditRouteString->setPlainText(routeStringWriter->createStringForRoute(NavApp::getRouteConst(),
                                                                                       NavApp::getRouteCruiseSpeedKts(),
                                                                                       options));
  else
    ui->plainTextEditRouteString->setPlainText(routeString);
}

rs::RouteStringOptions RouteStringDialog::getOptionsFromSettings()
{
  return rs::RouteStringOptions(atools::settings::Settings::instance().
                                valueInt(lnm::ROUTE_STRING_DIALOG_OPTIONS, rs::DEFAULT_OPTIONS));
}

void RouteStringDialog::textChanged()
{
  // Calls RouteStringDialog::textChangedDelayed()
  textUpdateTimer.start(TEXT_CHANGE_DELAY_MS);
}

void RouteStringDialog::textChangedDelayed()
{
  qDebug() << Q_FUNC_INFO;

  flightplan->clear();

  QGuiApplication::setOverrideCursor(Qt::WaitCursor);
  routeStringReader->createRouteFromString(ui->plainTextEditRouteString->toPlainText(), options | rs::REPORT, flightplan, nullptr,
                                           &speedKts, &altitudeIncluded);
  QGuiApplication::restoreOverrideCursor();

  // Fill report into widget
  ui->textEditRouteStringErrors->clear();
  if(!routeStringReader->getMessages().isEmpty())
  {
    for(const QString& err : routeStringReader->getMessages())
      ui->textEditRouteStringErrors->append(err + "<br/>");
  }

  // Avoid update issues with macOS and mac style - force repaint
  ui->textEditRouteStringErrors->repaint();

  updateButtonState();
}

void RouteStringDialog::fromClipboardClicked()
{
  ui->plainTextEditRouteString->setPlainText(rs::cleanRouteString(QGuiApplication::clipboard()->text()).join(" "));
  textChangedDelayed();
}

void RouteStringDialog::toClipboardClicked()
{
  QGuiApplication::clipboard()->setText(ui->plainTextEditRouteString->toPlainText());
}

void RouteStringDialog::updateFlightplan()
{
  // Update type from current combox box setting
  // Low / high altitude is set later when resolving the airways

  if(ui->comboBoxRouteStringFlightplanType->currentIndex() == 0)
    flightplan->setFlightplanType(apln::IFR);
  else
    flightplan->setFlightplanType(apln::VFR);
}

/* A button box button was clicked */
void RouteStringDialog::buttonBoxClicked(QAbstractButton *button)
{
  if(button == ui->buttonBoxRouteString->button(QDialogButtonBox::Ok))
  {
    updateFlightplan();
    QDialog::accept();
  }
  else if(button == ui->buttonBoxRouteString->button(QDialogButtonBox::Help))
    HelpHandler::openHelpUrlWeb(parentWidget(), lnm::helpOnlineUrl + "ROUTEDESCR.html", lnm::helpLanguageOnline());
  else if(button == ui->buttonBoxRouteString->button(QDialogButtonBox::Close))
    QDialog::reject();
}

void RouteStringDialog::updateButtonState()
{

  ui->pushButtonRouteStringUpdate->setEnabled(!NavApp::getRouteConst().isEmpty());

  ui->buttonBoxRouteString->button(QDialogButtonBox::Ok)->setDisabled(flightplan->getEntries().isEmpty());

  ui->pushButtonRouteStringToClipboard->setDisabled(ui->plainTextEditRouteString->toPlainText().isEmpty());

  ui->pushButtonRouteStringFromClipboard->setDisabled(QGuiApplication::clipboard()->text().simplified().isEmpty());

  // Copy option flags to dropdown menu items
  updatingActions = true;
  for(QAction *act : ui->toolButtonRouteStringOptions->menu()->actions())
    act->setChecked(rs::RouteStringOptions(act->data().toInt()) & options);
  updatingActions = false;
}
