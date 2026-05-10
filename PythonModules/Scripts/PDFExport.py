import html
import json
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile
from datetime import datetime
from pathlib import Path

from shared_paths import resolve_temporary_dir


TEMPLATE_RELATIVE_PATH = Path("Generate") / "report" / "AFDROIS.html"
TOKEN_PATTERN = re.compile(r"(\{\{.*?\}\}|\{%.*?%\})", re.DOTALL)
UNAVAILABLE_TEXT = "未提供"
MISSING_NUMERIC_TEXT = "--"
LOW_FREQUENCY_BOUNDARY_HZ = 300.0


class MissingValue(str):
    def __new__(cls, value: str = UNAVAILABLE_TEXT):
        return super().__new__(cls, value)

    def __bool__(self) -> bool:
        return False

    def __contains__(self, item) -> bool:
        return False


class SafeContext(dict):
    def __missing__(self, key):
        return MissingValue()


class TemplateObject(dict):
    def __getattr__(self, item):
        if item in self:
            return self[item]
        return MissingValue()


def resolve_template_path() -> Path:
    configured_path = os.environ.get("BSSAS_REPORT_TEMPLATE_PATH", "").strip()
    if configured_path:
        candidate = Path(configured_path)
        if candidate.exists():
            return candidate

    script_dir = Path(__file__).resolve().parent
    candidate_paths = [
        script_dir.parent.parent / TEMPLATE_RELATIVE_PATH,
        script_dir.parent.parent / "distribution" / TEMPLATE_RELATIVE_PATH,
    ]

    checked_paths = []
    for candidate in candidate_paths:
        resolved_candidate = candidate.resolve()
        if resolved_candidate in checked_paths:
            continue
        checked_paths.append(resolved_candidate)
        if resolved_candidate.exists():
            return resolved_candidate

    searched = ", ".join(str(path) for path in checked_paths)
    raise FileNotFoundError(f"report template not found. searched: {searched}")


def resolve_browser_path() -> Path:
    configured = os.environ.get("BSSAS_EDGE_PATH", "").strip()
    if configured:
        configured_path = Path(configured)
        if configured_path.exists():
            return configured_path

    browser_candidates = [
        shutil.which("msedge"),
        shutil.which("msedge.exe"),
        shutil.which("chrome"),
        shutil.which("chrome.exe"),
        r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
        r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
        r"C:\Program Files\Google\Chrome\Application\chrome.exe",
    ]

    for candidate in browser_candidates:
        if not candidate:
            continue
        candidate_path = Path(candidate)
        if candidate_path.exists():
            return candidate_path

    raise FileNotFoundError(
        "no supported browser found; install Microsoft Edge/Chrome or set BSSAS_EDGE_PATH"
    )


def first_present(*values):
    for value in values:
        if value is None:
            continue
        if isinstance(value, str) and not value.strip():
            continue
        return value
    return None


def to_float(value, default: float = 0.0) -> float:
    if isinstance(value, bool):
        return float(value)

    if isinstance(value, (int, float)):
        number = float(value)
        return number if math.isfinite(number) else default

    if isinstance(value, str):
        candidate = value.replace(",", "").strip()
        if not candidate:
            return default
        match = re.search(r"-?\d+(?:\.\d+)?", candidate)
        if match:
            try:
                return float(match.group(0))
            except ValueError:
                return default

    return default


def to_int(value, default: int = 0) -> int:
    try:
        return int(round(to_float(value, float(default))))
    except (TypeError, ValueError):
        return default


def ensure_list(value):
    if value is None:
        return []

    if isinstance(value, list):
        return value

    if isinstance(value, tuple):
        return list(value)

    if isinstance(value, str):
        candidate = value.strip()
        if not candidate or candidate == UNAVAILABLE_TEXT:
            return []
        return [item.strip() for item in re.split(r"[,;/，；、]", candidate) if item.strip()]

    return [value]


def mean(values) -> float:
    cleaned = []
    for value in values:
        try:
            number = float(value)
        except (TypeError, ValueError):
            continue
        if math.isfinite(number):
            cleaned.append(number)

    if not cleaned:
        return 0.0
    return sum(cleaned) / len(cleaned)


def format_number(
    value,
    digits: int = 2,
    default: str = MISSING_NUMERIC_TEXT,
    trim: bool = True,
) -> str:
    try:
        number = float(value)
    except (TypeError, ValueError):
        return default

    if not math.isfinite(number):
        return default

    formatted = f"{number:.{digits}f}"
    if trim and "." in formatted:
        formatted = formatted.rstrip("0").rstrip(".")
    return formatted if formatted else "0"


def parse_report_datetime(value) -> datetime:
    if isinstance(value, datetime):
        return value

    if isinstance(value, str):
        candidate = value.strip()
        for fmt in (
            "%Y-%m-%d %H:%M:%S",
            "%Y-%m-%d_%H-%M-%S",
            "%Y-%m-%d",
            "%Y/%m/%d %H:%M:%S",
            "%Y/%m/%d",
        ):
            try:
                return datetime.strptime(candidate, fmt)
            except ValueError:
                continue

    return datetime.now()


def extract_points(raw_points) -> list[tuple[float, float]]:
    points = []
    for item in ensure_list(raw_points):
        if isinstance(item, dict):
            x_value = first_present(item.get("x"), item.get("frequency"), item.get("time"))
            y_value = first_present(item.get("y"), item.get("value"), item.get("magnitude"))
        elif isinstance(item, (list, tuple)) and len(item) >= 2:
            x_value, y_value = item[0], item[1]
        else:
            continue

        x = to_float(x_value, math.nan)
        y = to_float(y_value, math.nan)
        if math.isfinite(x) and math.isfinite(y):
            points.append((x, y))

    return points


def extract_events(report_data: dict, feature_values: dict) -> list[dict]:
    for candidate in (
        report_data.get("recognizedSegments"),
        feature_values.get("recognizedSegments"),
    ):
        if isinstance(candidate, list):
            return [item for item in candidate if isinstance(item, dict)]
    return []


def collect_context_overrides(
    payload: dict,
    accepted_keys: set[str],
    blocked_keys=None,
) -> dict:
    overrides = {}
    blocked_keys = blocked_keys or set()

    def visit(node):
        if isinstance(node, dict):
            for key, value in node.items():
                if key in accepted_keys and key not in blocked_keys:
                    if isinstance(value, str) and not value.strip():
                        pass
                    elif value not in (None, [], {}):
                        overrides[key] = value
                visit(value)
        elif isinstance(node, list):
            for item in node:
                visit(item)

    visit(payload)
    return overrides


def weighted_frequency_at_ratio(
    frequency_points: list[tuple[float, float]],
    ratio: float,
) -> float:
    total_weight = sum(weight for _, weight in frequency_points)
    if total_weight <= 0.0:
        return 0.0

    threshold = total_weight * min(max(ratio, 0.0), 1.0)
    accumulated = 0.0
    for frequency, weight in frequency_points:
        accumulated += weight
        if accumulated >= threshold:
            return frequency

    return frequency_points[-1][0] if frequency_points else 0.0


def compute_spectrum_metrics(spectrum_points: list[tuple[float, float]]) -> dict:
    sanitized = []
    for frequency, magnitude in spectrum_points:
        if not math.isfinite(frequency) or not math.isfinite(magnitude):
            continue
        sanitized.append((max(frequency, 0.0), abs(magnitude)))

    if not sanitized:
        return {}

    sanitized.sort(key=lambda item: item[0])
    total_weight = sum(weight for _, weight in sanitized)
    if total_weight <= 0.0:
        return {}

    dominant_frequency = max(sanitized, key=lambda item: item[1])[0]
    centroid = sum(frequency * weight for frequency, weight in sanitized) / total_weight
    probabilities = [weight / total_weight for _, weight in sanitized if weight > 0.0]
    if probabilities and len(probabilities) > 1:
        entropy = -sum(probability * math.log(probability) for probability in probabilities)
        entropy /= math.log(len(probabilities))
    else:
        entropy = 0.0

    low_weight = sum(
        weight for frequency, weight in sanitized if frequency <= LOW_FREQUENCY_BOUNDARY_HZ
    )
    high_weight = max(total_weight - low_weight, 0.0)

    return {
        "dominant_frequency": dominant_frequency,
        "frequency_range_min": weighted_frequency_at_ratio(sanitized, 0.05),
        "frequency_range_max": weighted_frequency_at_ratio(sanitized, 0.95),
        "spectral_centroid": centroid,
        "spectral_entropy": entropy,
        "low_frequency_percent": (low_weight / total_weight) * 100.0,
        "high_low_ratio": high_weight / low_weight if low_weight > 0.0 else 0.0,
    }


def compute_event_metrics(
    events: list[dict],
    waveform_points: list[tuple[float, float]],
    total_duration_seconds: float,
) -> dict:
    durations_ms = [
        to_float(event.get("durationMs"), math.nan)
        for event in events
        if math.isfinite(to_float(event.get("durationMs"), math.nan))
    ]
    peak_amplitudes = [
        abs(to_float(event.get("peakAmplitude"), math.nan))
        for event in events
        if math.isfinite(to_float(event.get("peakAmplitude"), math.nan))
    ]
    rms_values = [
        abs(to_float(event.get("rms"), math.nan))
        for event in events
        if math.isfinite(to_float(event.get("rms"), math.nan))
    ]

    starts = [
        to_float(event.get("startSeconds"), math.nan)
        for event in events
        if math.isfinite(to_float(event.get("startSeconds"), math.nan))
    ]
    ends = [
        to_float(event.get("endSeconds"), math.nan)
        for event in events
        if math.isfinite(to_float(event.get("endSeconds"), math.nan))
    ]

    interval_values = []
    for index in range(1, min(len(starts), len(ends))):
        interval_values.append(max(0.0, starts[index] - ends[index - 1]))

    total_event_duration_ms = sum(durations_ms)
    if total_duration_seconds > 0.0:
        event_rate_per_min = len(events) * 60.0 / total_duration_seconds
        burst_rate = (total_event_duration_ms / (total_duration_seconds * 1000.0)) * 100.0
    else:
        event_rate_per_min = 0.0
        burst_rate = 0.0

    rms_energy = mean([value * value for value in rms_values])
    peak_energy = max((value * value for value in peak_amplitudes), default=0.0)

    if rms_energy <= 0.0 and waveform_points:
        waveform_amplitudes = [abs(y_value) for _, y_value in waveform_points]
        rms_energy = mean([value * value for value in waveform_amplitudes])
        peak_energy = max(
            peak_energy,
            max((value * value for value in waveform_amplitudes), default=0.0),
        )

    avg_interval = mean(interval_values)
    if avg_interval <= 0.0 and total_duration_seconds > 0.0 and not events:
        avg_interval = total_duration_seconds

    return {
        "event_count": len(events),
        "avg_duration_ms": mean(durations_ms),
        "avg_interval_s": avg_interval,
        "peak_amplitude": max(peak_amplitudes, default=0.0),
        "event_rate_per_min": event_rate_per_min,
        "rms_energy": rms_energy,
        "burst_rate": burst_rate,
        "peak_energy": peak_energy,
        "total_event_duration_ms": total_event_duration_ms,
    }


def frequency_deviation(value: float) -> str:
    if value < 4.0:
        return "Low"
    if value > 5.0:
        return "High"
    return "Normal"


def rms_deviation(value: float) -> str:
    if value < 0.001:
        return "Low"
    if value > 1.0:
        return "High"
    return "Normal"


def interval_deviation(value: float) -> str:
    if value < 12.0:
        return "Short"
    return "Normal" if value <= 15.0 else "Long"


def build_template_events(events: list[dict], event_rate_per_min: float) -> list[dict]:
    if not events:
        return [
            {
                "index": "-",
                "type": "未识别到肠鸣音事件",
                "start_time": MISSING_NUMERIC_TEXT,
                "end_time": MISSING_NUMERIC_TEXT,
                "duration": "当前样本中未检测到满足阈值条件的事件",
            }
        ]

    template_events = []
    for index, event in enumerate(events, start=1):
        event_type = first_present(
            event.get("eventType"),
            event.get("acousticEventType"),
            event.get("type"),
            "高频/亢进音" if event_rate_per_min > 5.0 else "典型肠鸣音",
        )
        template_events.append(
            {
                "index": str(index),
                "type": str(event_type),
                "start_time": format_number(event.get("startSeconds"), 3),
                "end_time": format_number(event.get("endSeconds"), 3),
                "duration": format_number(event.get("durationMs"), 3),
            }
        )

    return template_events


def build_afdrois_context(payload: dict, json_path: Path) -> dict:
    report_data = payload.get("reportData", {})
    cached_data = payload.get("cachedData", {})
    feature_values = cached_data.get("featureValues", {})
    template_context = report_data.get("templateContext", {})
    meta = payload.get("meta", {})

    report_datetime = parse_report_datetime(
        first_present(meta.get("generatedAt"), template_context.get("generated_at"))
    )
    report_id = first_present(
        template_context.get("report_id"),
        meta.get("reportId"),
        f"AFDROIS-{json_path.stem}",
    )
    record_date = first_present(
        template_context.get("record_date"),
        report_datetime.strftime("%Y-%m-%d"),
    )
    data_source = first_present(
        meta.get("dataSource"),
        template_context.get("data_source"),
        "导入 WAV 文件",
    )

    waveform_cache = cached_data.get("waveform", {})
    spectrum_cache = cached_data.get("spectrum", {})
    waveform_points = extract_points(waveform_cache.get("downsampledPoints"))
    spectrum_points = extract_points(spectrum_cache.get("points"))
    events = extract_events(report_data, feature_values)

    sample_rate = to_int(
        first_present(
            feature_values.get("sampleRate"),
            waveform_cache.get("sampleRate"),
            template_context.get("sample_rate"),
        )
    )
    sample_count = to_int(
        first_present(feature_values.get("sampleCount"), template_context.get("sample_count"))
    )
    total_duration_seconds = to_float(
        first_present(
            waveform_cache.get("durationSeconds"),
            template_context.get("signal_duration_seconds"),
        )
    )
    if total_duration_seconds <= 0.0 and sample_rate > 0 and sample_count > 0:
        total_duration_seconds = sample_count / sample_rate

    ste_values = ensure_list(feature_values.get("steValues"))
    recognized_event_count = len(events)
    if recognized_event_count <= 0:
        recognized_event_count = to_int(
            first_present(
                feature_values.get("recognizedEventCount"),
                feature_values.get("bowelSoundOccurrenceCount"),
                template_context.get("recognized_event_count"),
            )
        )

    event_metrics = compute_event_metrics(events, waveform_points, total_duration_seconds)
    spectrum_metrics = compute_spectrum_metrics(spectrum_points)
    recognized_total_duration_ms = to_float(feature_values.get("recognizedTotalDurationMs"))
    if recognized_total_duration_ms <= 0.0:
        recognized_total_duration_ms = event_metrics["total_event_duration_ms"]

    sample_rate_text = first_present(
        template_context.get("sample_rate"),
        f"{sample_rate} Hz" if sample_rate > 0 else UNAVAILABLE_TEXT,
    )
    sample_count_text = first_present(
        template_context.get("sample_count"),
        str(sample_count) if sample_count > 0 else UNAVAILABLE_TEXT,
    )
    duration_text = first_present(
        template_context.get("signal_duration_seconds"),
        f"{format_number(total_duration_seconds, 3)} s"
        if total_duration_seconds > 0.0
        else UNAVAILABLE_TEXT,
    )
    feature_frame_count_text = first_present(
        template_context.get("feature_frame_count"),
        str(len(ste_values)) if ste_values else UNAVAILABLE_TEXT,
    )
    has_bowel_sound_text = first_present(
        template_context.get("has_bowel_sound"),
        "是" if recognized_event_count > 0 else "否",
    )
    recognized_event_count_text = first_present(
        template_context.get("recognized_event_count"),
        str(recognized_event_count),
    )
    recognized_total_duration_text = first_present(
        template_context.get("recognized_total_duration_ms"),
        f"{format_number(recognized_total_duration_ms, 3)} ms"
        if recognized_total_duration_ms > 0.0
        else UNAVAILABLE_TEXT,
    )

    context = {
        "report_id": report_id,
        "record_date": str(record_date),
        "data_source": str(data_source),
        "has_bowel_sound": str(has_bowel_sound_text),
        "sample_rate": str(sample_rate_text),
        "sample_count": str(sample_count_text),
        "signal_duration_seconds": str(duration_text),
        "feature_frame_count": str(feature_frame_count_text),
        "recognized_event_count": str(recognized_event_count_text),
        "recognized_total_duration_ms": str(recognized_total_duration_text),
        "td_total_events": str(recognized_event_count),
        "td_events_per_min": format_number(event_metrics["event_rate_per_min"], 2),
        "td_avg_duration": format_number(event_metrics["avg_duration_ms"], 2),
        "td_avg_interval": format_number(event_metrics["avg_interval_s"], 2),
        "td_peak_amp": format_number(event_metrics["peak_amplitude"], 4),
        "fd_dom_freq": format_number(spectrum_metrics.get("dominant_frequency"), 2),
        "fd_freq_range": (
            f"{format_number(spectrum_metrics.get('frequency_range_min'), 2)} - "
            f"{format_number(spectrum_metrics.get('frequency_range_max'), 2)}"
            if spectrum_metrics
            else UNAVAILABLE_TEXT
        ),
        "fd_spec_centroid": format_number(spectrum_metrics.get("spectral_centroid"), 2),
        "fd_spec_entropy": format_number(spectrum_metrics.get("spectral_entropy"), 4),
        "ef_rms_energy": format_number(event_metrics["rms_energy"], 6),
        "ef_burst_rate": format_number(event_metrics["burst_rate"], 2),
        "ef_hl_ratio": format_number(spectrum_metrics.get("high_low_ratio"), 4),
        "sa_freq_curr": format_number(event_metrics["event_rate_per_min"], 2),
        "sa_freq_dev": frequency_deviation(event_metrics["event_rate_per_min"]),
        "sa_rms_curr": format_number(event_metrics["rms_energy"], 6),
        "sa_rms_normal": "0.0010 - 1.0000",
        "sa_rms_dev": rms_deviation(event_metrics["rms_energy"]),
        "sa_int_curr": format_number(event_metrics["avg_interval_s"], 2),
        "sa_int_dev": interval_deviation(event_metrics["avg_interval_s"]),
        "events": build_template_events(events, event_metrics["event_rate_per_min"]),
    }

    accepted_keys = set(context.keys())
    overrides = collect_context_overrides(payload, accepted_keys, {"events"})
    for key, value in overrides.items():
        context[key] = value

    return context


def to_template_value(value):
    if isinstance(value, dict):
        return TemplateObject({key: to_template_value(item) for key, item in value.items()})
    if isinstance(value, list):
        return [to_template_value(item) for item in value]
    if isinstance(value, tuple):
        return [to_template_value(item) for item in value]
    return value


def format_rendered_value(value) -> str:
    if value is None:
        return UNAVAILABLE_TEXT

    if isinstance(value, MissingValue):
        return html.escape(str(value))

    if isinstance(value, bool):
        return "是" if value else "否"

    if isinstance(value, (int, float)) and not isinstance(value, bool):
        return format_number(value)

    if isinstance(value, (list, tuple, set)):
        if not value:
            return UNAVAILABLE_TEXT
        return html.escape(", ".join(str(item) for item in value))

    return html.escape(str(value)).replace("\n", "<br>")


def compile_template(template_text: str) -> str:
    tokens = TOKEN_PATTERN.split(template_text)
    code_lines = [
        "def __render_template():",
        "    result = []",
        "    append = result.append",
    ]
    indent = 1

    for token in tokens:
        if not token:
            continue

        if token.startswith("{{") and token.endswith("}}"):
            expression = token[2:-2].strip()
            code_lines.append(
                "    " * indent + f"append(format_rendered_value({expression}))"
            )
            continue

        if token.startswith("{%") and token.endswith("%}"):
            statement = token[2:-2].strip()
            if statement.startswith("if "):
                code_lines.append("    " * indent + f"{statement}:")
                indent += 1
            elif statement.startswith("elif "):
                indent -= 1
                code_lines.append("    " * indent + f"{statement}:")
                indent += 1
            elif statement == "else":
                indent -= 1
                code_lines.append("    " * indent + "else:")
                indent += 1
            elif statement.startswith("for "):
                code_lines.append("    " * indent + f"{statement}:")
                indent += 1
            elif statement in {"endif", "endfor"}:
                indent = max(1, indent - 1)
            else:
                raise ValueError(f"unsupported template statement: {statement}")
            continue

        code_lines.append("    " * indent + f"append({token!r})")

    code_lines.append("    return ''.join(result)")
    return "\n".join(code_lines)


def render_template(template_text: str, context: dict) -> str:
    compiled_source = compile_template(template_text)
    execution_context = SafeContext(
        {
            "__builtins__": {},
            "format_rendered_value": format_rendered_value,
        }
    )
    execution_context.update(
        {key: to_template_value(value) for key, value in context.items()}
    )
    exec(compiled_source, execution_context)
    return execution_context["__render_template"]()


def create_html_from_json(json_path: Path) -> str:
    with json_path.open("r", encoding="utf-8-sig") as file:
        payload = json.load(file)

    template_path = resolve_template_path()
    template_text = template_path.read_text(encoding="utf-8-sig")
    context = build_afdrois_context(payload, json_path)
    return render_template(template_text, context)


def export_pdf_with_browser(html_text: str, pdf_path: Path) -> None:
    browser_path = resolve_browser_path()
    pdf_path.parent.mkdir(parents=True, exist_ok=True)
    if pdf_path.exists():
        pdf_path.unlink()

    temp_html_path = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            suffix=".html",
            encoding="utf-8",
            delete=False,
            prefix="pdf_export_",
            dir=resolve_temporary_dir(),
        ) as temp_html_file:
            temp_html_file.write(html_text)
            temp_html_path = Path(temp_html_file.name)

        command = [
            str(browser_path),
            "--headless",
            "--disable-gpu",
            "--no-first-run",
            "--print-to-pdf-no-header",
            f"--print-to-pdf={pdf_path}",
            temp_html_path.resolve().as_uri(),
        ]

        process = subprocess.run(
            command,
            capture_output=True,
            text=True,
            timeout=180,
            check=False,
        )
        if process.returncode != 0:
            raise RuntimeError(
                "browser PDF export failed. "
                f"stdout: {process.stdout.strip()} stderr: {process.stderr.strip()}"
            )

        if not pdf_path.exists() or pdf_path.stat().st_size <= 0:
            raise RuntimeError("browser finished without creating a valid PDF file")
    finally:
        if temp_html_path is not None and temp_html_path.exists():
            temp_html_path.unlink()


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: PDFExport.py <report_json_path> <output_pdf_path>", file=sys.stderr)
        return 1

    json_path = Path(sys.argv[1]).resolve()
    pdf_path = Path(sys.argv[2]).resolve()

    if not json_path.exists():
        print(f"report json not found: {json_path}", file=sys.stderr)
        return 1

    try:
        html_text = create_html_from_json(json_path)
        export_pdf_with_browser(html_text, pdf_path)
        print(f"pdf generated: {pdf_path}")
        return 0
    except Exception as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
