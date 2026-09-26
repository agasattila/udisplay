// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

/**
 * Parse a decompressed uDisplay YAML blob into a list of WidgetDef.
 *
 * Widget ID assignment matches udisplay-gen exactly:
 *   1. Collect all key paths — every widget (IdScheme::EveryWidget, issue
 *      #43), or only value-bearing widgets for pre-v5 devices
 *      (IdScheme::LeafOnly).
 *   2. Sort alphabetically.
 *   3. Assign IDs 0x10, 0x11, ... in sort order.
 *
 * Device name and version are returned via out-parameters.
 */
#pragma once

#include "Protocol.h"
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

    /* Widget ID derivation scheme. The YAML blob does not say which scheme
     * the firmware's generated header used — the device's HANDSHAKE
     * proto_version does:
     *   LeafOnly    (proto < 0x05): containers (section/row/grid/dpad) and
     *               decorations (label/separator) get no ID (widgetId 0).
     *   EveryWidget (proto >= 0x05): every widget gets an ID; containers
     *               are still transparent to their children's ID paths. */
    enum class IdScheme { LeafOnly, EveryWidget };

    static IdScheme idSchemeForProtoVersion(uint8_t protoVersion)
    {
        return protoVersion >= Proto::PROTO_VERSION_EVERY_WIDGET_ID
            ? IdScheme::EveryWidget : IdScheme::LeafOnly;
    }

    /** Scheme used by subsequent parse() calls. Default: EveryWidget. */
    void setIdScheme(IdScheme scheme) { m_idScheme = scheme; }
    IdScheme idScheme() const { return m_idScheme; }

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
    IdScheme               m_idScheme = IdScheme::EveryWidget;
    QString                m_error;
    QList<ParseDiagnostic> m_diagnostics;
};

Q_DECLARE_METATYPE(YamlParser::ParseDiagnostic)
Q_DECLARE_METATYPE(QList<YamlParser::ParseDiagnostic>)
