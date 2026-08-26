#include "localization_controller.hpp"

#include <QCoreApplication>
#include <QLocale>
#include <QQmlEngine>
#include <QSettings>

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
