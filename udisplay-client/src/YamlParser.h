// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

/**
 * Parse a decompressed uDisplay YAML blob into a list of WidgetDef.
 *
 * Widget ID assignment matches udisplay-gen exactly:
 *   1. Collect all key paths (top-level, button children, button-group items).
 *   2. Sort alphabetically.
 *   3. Assign IDs 0x10, 0x11, ... in sort order.
 *
 * Device name and version are returned via out-parameters.
 */
#pragma once

#include "WidgetDef.h"
#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QMetaType>

class YamlParser
{
public:
    enum class Severity { Warning, Error };

    struct ParseDiagnostic {
        Severity severity;
        QString  widgetKey;
        QString  field;
        QString  message;
    };

    /**
     * Parse YAML from raw (decompressed) bytes.
     *
     * Returns true on success.  On failure, errorString() describes the problem.
     * widgetsOut contains one WidgetDef per widget (including children / items
     * with their own widget IDs).
     * stylesOut is populated from the optional top-level 'style:' block.
     * Always contains at least a "default" entry with C++ hardcoded defaults.
     *
     * Warnings are accumulated in diagnostics() even on success.
     * The first Error diagnostic sets errorString() and causes false to be returned.
     */
    bool parse(const QByteArray& yamlBytes,
               QList<WidgetDef>& widgetsOut,
               QString& deviceNameOut,
               QString& deviceVersionOut,
               QStringList& capabilitiesOut,
               QMap<QString, StyleToken>& stylesOut);

    /** Convenience overload — discards capabilities and styles. */
    bool parse(const QByteArray& yamlBytes,
               QList<WidgetDef>& widgetsOut,
               QString& deviceNameOut,
               QString& deviceVersionOut)
    {
        QStringList caps;
        QMap<QString, StyleToken> styles;
        return parse(yamlBytes, widgetsOut, deviceNameOut, deviceVersionOut, caps, styles);
    }

    /** Convenience overload — discards styles. */
    bool parse(const QByteArray& yamlBytes,
               QList<WidgetDef>& widgetsOut,
               QString& deviceNameOut,
               QString& deviceVersionOut,
               QStringList& capabilitiesOut)
    {
        QMap<QString, StyleToken> styles;
        return parse(yamlBytes, widgetsOut, deviceNameOut, deviceVersionOut,
                     capabilitiesOut, styles);
    }

    QString errorString() const { return m_error; }

    /** Diagnostics accumulated during the most recent parse() call.
     *  Cleared at the start of each parse(). Warnings are present even on success. */
    QList<ParseDiagnostic> diagnostics() const { return m_diagnostics; }

private:
    QString                m_error;
    QList<ParseDiagnostic> m_diagnostics;
};

Q_DECLARE_METATYPE(YamlParser::ParseDiagnostic)
Q_DECLARE_METATYPE(QList<YamlParser::ParseDiagnostic>)
