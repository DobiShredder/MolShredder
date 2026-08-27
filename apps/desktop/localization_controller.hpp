#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTranslator>
#include <QVariantMap>

class QQmlEngine;

namespace molshredder::desktop {

class LocalizationController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString currentLanguage READ currentLanguage NOTIFY
                 currentLanguageChanged)
  Q_PROPERTY(QStringList supportedLanguages READ supportedLanguages CONSTANT)

 public:
  explicit LocalizationController(QObject* parent = nullptr);

  [[nodiscard]] QString currentLanguage() const;
  [[nodiscard]] QStringList supportedLanguages() const;
  [[nodiscard]] bool applyInitialLanguage(const QString& command_line_language);
  void setEngine(QQmlEngine* engine);

  Q_INVOKABLE bool setLanguage(const QString& language);
  Q_INVOKABLE QVariantMap actionMetadata(const QString& action_id) const;
  Q_INVOKABLE QString translateUi(const QString& source) const;

 signals:
  void currentLanguageChanged();

 private:
  [[nodiscard]] static QString canonicalLanguage(const QString& language);
  bool applyLanguage(const QString& language, bool persist);

  QTranslator translator_;
  QQmlEngine* engine_{};
  QString current_language_{QStringLiteral("en")};
};

}  // namespace molshredder::desktop
