/**
 * WidgetDump — textual dump of the fully-resolved widget model, for --debug.
 *
 * Pure function: no Qt object lifecycle, no dependency on DeviceController or
 * WidgetModel. Takes the same QList<WidgetDef> that YamlParser::parse() and
 * DeviceController::applyParsedYaml()/onBootstrapSucceeded() already produce.
 *
 * Prints every field relevant to each widget's type (mirroring the per-type
 * switch in WidgetModel::buildPropsMap), plus fields that map is deliberately
 * reduced from (defaultTextMode, sectionOwnerRow) since a debug dump is meant
 * to show full internal state, not just what QML needs to render.
 *
 * value and debugValue are printed as two separate fields: value only ever
 * carries a device-pushed STATE_UPDATE (real mode) or a design-mode preview
 * copied into WidgetModel's own storage (never back into the caller's
 * QList<WidgetDef>) — so it is legitimately empty at dump time in both modes.
 * debugValue is populated directly by YamlParser from debug_state: at parse
 * time and is already present in the list passed here; omitting it would make
 * the one design-mode-specific piece of state invisible in a tool built
 * specifically for design-mode debugging.
 */
#pragma once

#include "WidgetDef.h"
#include <QList>
#include <QString>

QString dumpWidgetTree(const QList<WidgetDef>& widgets,
                        const QString& deviceName,
                        const QString& version,
                        const QString& activeStyle);
