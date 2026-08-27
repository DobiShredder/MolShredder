#include "localization_controller.hpp"

#include <QCoreApplication>
#include <QLocale>
#include <QQmlEngine>
#include <QSettings>

#include "molshredder/gui/action_catalog.hpp"

namespace molshredder::desktop {
namespace {

constexpr auto kLanguageSetting = "ui/language";

}  // namespace

LocalizationController::LocalizationController(QObject* parent)
    : QObject{parent} {}

QString LocalizationController::currentLanguage() const {
  return current_language_;
}

QStringList LocalizationController::supportedLanguages() const {
  return {QStringLiteral("en"), QStringLiteral("ko")};
}

QString LocalizationController::canonicalLanguage(const QString& language) {
  const auto normalized = language.trimmed().toLower().replace(u'_', u'-');
  if (normalized.isEmpty() || normalized == QStringLiteral("system")) {
    return QLocale::system().language() == QLocale::Korean
               ? QStringLiteral("ko")
               : QStringLiteral("en");
  }
  if (normalized == QStringLiteral("en") ||
      normalized.startsWith(QStringLiteral("en-"))) {
    return QStringLiteral("en");
  }
  if (normalized == QStringLiteral("ko") ||
      normalized.startsWith(QStringLiteral("ko-"))) {
    return QStringLiteral("ko");
  }
  return {};
}

bool LocalizationController::applyInitialLanguage(
    const QString& command_line_language) {
  const auto requested = command_line_language.isEmpty()
                             ? QSettings{}.value(kLanguageSetting,
                                                 QStringLiteral("system"))
                                   .toString()
                             : command_line_language;
  return applyLanguage(requested, false);
}

void LocalizationController::setEngine(QQmlEngine* engine) {
  engine_ = engine;
}

bool LocalizationController::setLanguage(const QString& language) {
  return applyLanguage(language, true);
}

QVariantMap LocalizationController::actionMetadata(
    const QString& action_id) const {
  const auto id = action_id.toStdString();
  const auto* action = gui::find_action_metadata(id);
  if (action == nullptr) return {};
  QStringList surfaces;
  if ((action->surfaces & gui::surface_mask(gui::ActionSurface::menu)) != 0U)
    surfaces.push_back(QStringLiteral("menu"));
  if ((action->surfaces & gui::surface_mask(gui::ActionSurface::toolbar)) !=
      0U)
    surfaces.push_back(QStringLiteral("toolbar"));
  if ((action->surfaces &
       gui::surface_mask(gui::ActionSurface::command_palette)) != 0U)
    surfaces.push_back(QStringLiteral("command-palette"));
  if ((action->surfaces & gui::surface_mask(gui::ActionSurface::panel)) != 0U)
    surfaces.push_back(QStringLiteral("panel"));
  if ((action->surfaces &
       gui::surface_mask(gui::ActionSurface::context_menu)) != 0U)
    surfaces.push_back(QStringLiteral("context-menu"));
  QStringList requirements;
  if ((action->requirements &
       gui::requirement_mask(gui::ActionRequirement::workspace)) != 0U)
    requirements.push_back(QStringLiteral("workspace"));
  if ((action->requirements &
       gui::requirement_mask(gui::ActionRequirement::selection)) != 0U)
    requirements.push_back(QStringLiteral("selection"));
  if ((action->requirements &
       gui::requirement_mask(gui::ActionRequirement::trajectory)) != 0U)
    requirements.push_back(QStringLiteral("trajectory"));
  if ((action->requirements &
       gui::requirement_mask(gui::ActionRequirement::volume)) != 0U)
    requirements.push_back(QStringLiteral("volume"));
  return {{QStringLiteral("id"), QString::fromUtf8(action->id)},
          {QStringLiteral("command"),
           QString::fromUtf8(action->command_name)},
          {QStringLiteral("alternateCommand"),
           QString::fromUtf8(action->alternate_command_name)},
          {QStringLiteral("menu"), QString::fromUtf8(action->menu)},
          {QStringLiteral("label"),
           QString::fromUtf8(action->label_source)},
          {QStringLiteral("status"),
           QString::fromUtf8(action->status_source)},
          {QStringLiteral("error"),
           QString::fromUtf8(action->error_source)},
          {QStringLiteral("keywords"),
           QString::fromUtf8(action->keywords_source)},
          {QStringLiteral("unavailable"),
           QString::fromUtf8(action->unavailable_source)},
          {QStringLiteral("shortcut"),
           QString::fromUtf8(action->shortcut)},
          {QStringLiteral("helpTarget"),
           QString::fromUtf8(action->help_target)},
          {QStringLiteral("parameterGroup"),
           QString::fromUtf8(action->parameter_group)},
          {QStringLiteral("order"), action->order},
          {QStringLiteral("surfaces"), surfaces},
          {QStringLiteral("requirements"), requirements},
          {QStringLiteral("checkable"), action->checkable},
          {QStringLiteral("requiresWorkspace"),
           requirements.contains(QStringLiteral("workspace"))}};
}

QString LocalizationController::translateUi(const QString& source) const {
  const auto utf8 = source.toUtf8();
  return QCoreApplication::translate("Main", utf8.constData());
}

bool LocalizationController::applyLanguage(const QString& language,
                                            bool persist) {
  const auto canonical = canonicalLanguage(language);
  if (canonical.isEmpty()) return false;

  QCoreApplication::removeTranslator(&translator_);
  if (canonical == QStringLiteral("ko")) {
    if (!translator_.load(QStringLiteral(":/i18n/molshredder_ko.qm"))) {
      return false;
    }
    QCoreApplication::installTranslator(&translator_);
    QLocale::setDefault(QLocale{QStringLiteral("ko_KR")});
  } else {
    QLocale::setDefault(QLocale{QStringLiteral("en_US")});
  }

  const auto changed = current_language_ != canonical;
  current_language_ = canonical;
  if (persist) QSettings{}.setValue(kLanguageSetting, canonical);
  if (changed) emit currentLanguageChanged();
  if (engine_ != nullptr) engine_->retranslate();
  return true;
}

}  // namespace molshredder::desktop
