/*
 * Copyright (C) 2026, Robert Patterson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "mnx_gaps.h"

#include <algorithm>
#include <string>

#include "core/denigma.h"
#include "denigma/classify/chords.h"
#include "mnx.h"

namespace denigma {
namespace formats {
namespace mnx {
namespace detail {

namespace {

std::string pitchStep(music_theory::NoteName noteName)
{
    using NoteName = music_theory::NoteName;
    switch (noteName) {
    case NoteName::A: return "A";
    case NoteName::B: return "B";
    case NoteName::C: return "C";
    case NoteName::D: return "D";
    case NoteName::E: return "E";
    case NoteName::F: return "F";
    case NoteName::G: return "G";
    }
    return "C";
}

json pitchJson(const classify::chord::Pitch& pitch)
{
    return {
        { "step", pitchStep(pitch.step) },
        { "alteration", pitch.alteration }
    };
}

json suffixJson(const classify::ChordSuffixClassification& suffix)
{
    auto degrees = json::array();
    for (const auto& degree : suffix.degrees) {
        degrees.push_back({
            { "value", degree.value },
            { "alteration", degree.alteration },
            { "type", classify::chordDegreeTypeName(degree.type) },
            { "impliedByText", degree.impliedByText }
        });
    }
    json result{
        { "suffixText", suffix.calcText() },
        { "degrees", std::move(degrees) },
        { "parenthesizeDegrees", suffix.parenthesizeDegrees },
        { "stackDegrees", suffix.stackDegrees },
        { "hasOuterParentheses", suffix.hasOuterParentheses },
        { "hasUnrecognizedGlyphs", suffix.hasUnrecognizedGlyphs }
    };
    result["quality"] = suffix.quality
        ? json(classify::chordQualityName(*suffix.quality))
        : json(nullptr);
    return result;
}

json chordJson(const classify::ChordSymbolClassification& chord)
{
    auto result = suffixJson(chord.suffix);
    result["root"] = pitchJson(chord.root);
    result["rootLowerCase"] = chord.rootLowerCase;
    result["showRoot"] = chord.showRoot;
    result["showSuffix"] = chord.showSuffix;
    if (chord.bass) {
        result["bass"] = pitchJson(*chord.bass);
        result["bassLowerCase"] = chord.bassLowerCase;
    }
    if (chord.bassArrangement) {
        result["bassArrangement"] = classify::chordBassArrangementName(*chord.bassArrangement);
    }
    return result;
}

} // namespace

void reportChordSymbolGaps(
    const std::shared_ptr<MnxMusxMapping>& context,
    std::string_view measureId,
    std::optional<int> staff,
    const MusxInstance<others::Measure>& musxMeasure,
    StaffCmper staffId)
{
    const auto assignments = context->document->getDetails()->getArray<details::ChordAssign>(
        musxMeasure->getRequestedPartId(), staffId, musxMeasure->getCmper());
    if (assignments.empty()) {
        return;
    }
    const auto keySignature = musxMeasure->createKeySignature(staffId);
    for (const auto& assignment : assignments) {
        const auto classification = classify::classifyChordSymbol(
            assignment, keySignature, KeySignature::KeyContext::Written);
        if (!classification) {
            context->logMessage(LogMsg() << "could not classify chord symbol in measure "
                << musxMeasure->getCmper() << ", staff " << staffId << ".", MessageSeverity::Warning);
            continue;
        }
        const auto position = Fraction::fromEdu((std::max)(Edu{}, assignment->horzEdu));
        json gap{
            { "type", "chord-symbol" },
            { "anchor", measureId },
            { "position", {
                { "numerator", position.numerator() },
                { "denominator", position.denominator() }
            } },
            { "chord", chordJson(*classification) }
        };
        if (staff) {
            gap["staff"] = *staff;
        }
        context->gaps.push_back(std::move(gap));
    }
}

void reportNoteheadGap(
    const std::shared_ptr<MnxMusxMapping>& context,
    std::string_view noteId,
    const classify::NoteheadClassification& classification,
    bool defaultsToFilled)
{
    using Fill = classify::notehead::Fill;
    using Shape = classify::notehead::Shape;
    const bool fillOverridesDefault = (classification.fill == Fill::Filled && !defaultsToFilled)
        || (classification.fill == Fill::Unfilled && defaultsToFilled);
    if (!classification || (classification.shape == Shape::Regular && !fillOverridesDefault)) {
        return;
    }
    json notehead{
        { "shape", classify::noteheadShapeName(classification.shape) },
        { "fill", classify::noteheadFillName(classification.fill) }
    };
    if (classification.glyphName) {
        notehead["glyph"] = *classification.glyphName;
    }
    context->gaps.push_back({
        { "type", "notehead" },
        { "anchor", noteId },
        { "notehead", std::move(notehead) }
    });
}

void finalizeGapReport(const std::shared_ptr<MnxMusxMapping>& context)
{
    if (!context->denigmaContext->conversionResult) {
        return;
    }
    const json report{
        { "schemaVersion", 1 },
        { "producer", {
            { "name", DENIGMA_NAME },
            { "version", DENIGMA_VERSION },
            { "commit", gitCommit() }
        } },
        { "gaps", context->gaps }
    };
    context->denigmaContext->conversionResult->setGapReport(report.dump(2));
}

} // namespace detail
} // namespace mnx
} // namespace formats
} // namespace denigma