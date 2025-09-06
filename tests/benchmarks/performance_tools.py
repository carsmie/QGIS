#!/usr/bin/env python3
"""
QGIS Performance Measurement Tools

Utilities for measuring and analyzing QGIS performance metrics.
Provides tools for:
- Timing measurements
- Memory usage tracking  
- Benchmark result comparison
- Performance report generation
"""

import json
import os
import psutil
import time
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional, Any
import subprocess
import sys


class PerformanceTimer:
    """High-precision timing utility for performance measurements."""
    
    def __init__(self, name: str = "timer"):
        self.name = name
        self.start_time: Optional[float] = None
        self.end_time: Optional[float] = None
        self.measurements: List[Dict[str, Any]] = []
        
    def start(self) -> None:
        """Start timing measurement."""
        self.start_time = time.perf_counter()
        
    def stop(self) -> float:
        """Stop timing and return elapsed time in milliseconds."""
        if self.start_time is None:
            raise RuntimeError("Timer not started")
            
        self.end_time = time.perf_counter()
        elapsed_ms = (self.end_time - self.start_time) * 1000
        
        self.measurements.append({
            "timestamp": datetime.now().isoformat(),
            "elapsed_ms": elapsed_ms,
            "start_time": self.start_time,
            "end_time": self.end_time
        })
        
        return elapsed_ms
        
    def get_average(self) -> float:
        """Get average time across all measurements."""
        if not self.measurements:
            return 0.0
        return sum(m["elapsed_ms"] for m in self.measurements) / len(self.measurements)
        
    def get_stats(self) -> Dict[str, float]:
        """Get comprehensive timing statistics."""
        if not self.measurements:
            return {"count": 0, "avg": 0.0, "min": 0.0, "max": 0.0}
            
        times = [m["elapsed_ms"] for m in self.measurements]
        return {
            "count": len(times),
            "avg": sum(times) / len(times),
            "min": min(times),
            "max": max(times),
            "total": sum(times)
        }


class MemoryTracker:
    """Track memory usage during performance tests."""
    
    def __init__(self):
        self.process = psutil.Process()
        self.baseline_memory: Optional[float] = None
        self.peak_memory: float = 0.0
        self.measurements: List[Dict[str, Any]] = []
        
    def set_baseline(self) -> None:
        """Set current memory usage as baseline."""
        self.baseline_memory = self.get_current_memory()
        self.peak_memory = self.baseline_memory
        
    def get_current_memory(self) -> float:
        """Get current memory usage in MB."""
        return self.process.memory_info().rss / (1024 * 1024)
        
    def record_measurement(self, label: str = "") -> Dict[str, float]:
        """Record current memory state."""
        current_memory = self.get_current_memory()
        if current_memory > self.peak_memory:
            self.peak_memory = current_memory
            
        measurement = {
            "timestamp": datetime.now().isoformat(),
            "label": label,
            "current_mb": current_memory,
            "baseline_mb": self.baseline_memory or 0.0,
            "delta_mb": current_memory - (self.baseline_memory or 0.0),
            "peak_mb": self.peak_memory
        }
        
        self.measurements.append(measurement)
        return measurement
        
    def get_stats(self) -> Dict[str, float]:
        """Get memory usage statistics."""
        if not self.measurements:
            return {"baseline": 0.0, "peak": 0.0, "current": 0.0, "max_delta": 0.0}
            
        deltas = [m["delta_mb"] for m in self.measurements]
        return {
            "baseline": self.baseline_memory or 0.0,
            "peak": self.peak_memory,
            "current": self.get_current_memory(),
            "max_delta": max(deltas) if deltas else 0.0
        }


class BenchmarkRunner:
    """Run and manage performance benchmarks."""
    
    def __init__(self, output_dir: str = "./benchmark_results"):
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(parents=True, exist_ok=True)
        self.results: List[Dict[str, Any]] = []
        
    def run_project_loading_benchmark(self, project_path: str, iterations: int = 3) -> Dict[str, Any]:
        """Benchmark QGIS project loading performance."""
        print(f"Running project loading benchmark: {project_path}")
        
        timer = PerformanceTimer("project_loading")
        memory_tracker = MemoryTracker()
        memory_tracker.set_baseline()
        
        project_size = Path(project_path).stat().st_size / (1024 * 1024)
        
        results = {
            "benchmark_type": "project_loading",
            "project_path": project_path,
            "project_size_mb": project_size,
            "iterations": iterations,
            "measurements": []
        }
        
        for i in range(iterations):
            print(f"  Iteration {i+1}/{iterations}")
            
            # Measure project loading time
            timer.start()
            
            # Run QGIS project loading (simulate for now)
            # In real implementation, this would call QGIS API
            elapsed = self._simulate_project_loading(project_size)
            
            actual_elapsed = timer.stop()
            memory_after = memory_tracker.record_measurement(f"iter_{i+1}")
            
            results["measurements"].append({
                "iteration": i + 1,
                "elapsed_ms": actual_elapsed,
                "memory_after": memory_after
            })
            
        # Calculate statistics
        timing_stats = timer.get_stats()
        memory_stats = memory_tracker.get_stats()
        
        results.update({
            "timing_stats": timing_stats,
            "memory_stats": memory_stats,
            "timestamp": datetime.now().isoformat()
        })
        
        self.results.append(results)
        return results
        
    def run_vector_rendering_benchmark(self, fgb_path: str, iterations: int = 10) -> Dict[str, Any]:
        """Benchmark vector rendering performance."""
        print(f"Running vector rendering benchmark: {fgb_path}")
        
        timer = PerformanceTimer("vector_rendering")
        memory_tracker = MemoryTracker()
        memory_tracker.set_baseline()
        
        fgb_size = Path(fgb_path).stat().st_size / (1024 * 1024)
        
        results = {
            "benchmark_type": "vector_rendering",
            "fgb_path": fgb_path,
            "fgb_size_mb": fgb_size,
            "iterations": iterations,
            "measurements": []
        }
        
        for i in range(iterations):
            print(f"  Iteration {i+1}/{iterations}")
            
            timer.start()
            
            # Simulate vector rendering
            elapsed = self._simulate_vector_rendering(fgb_size)
            
            actual_elapsed = timer.stop()
            memory_after = memory_tracker.record_measurement(f"iter_{i+1}")
            
            results["measurements"].append({
                "iteration": i + 1,
                "elapsed_ms": actual_elapsed,
                "memory_after": memory_after
            })
            
        # Calculate statistics
        timing_stats = timer.get_stats()
        memory_stats = memory_tracker.get_stats()
        
        results.update({
            "timing_stats": timing_stats,
            "memory_stats": memory_stats,
            "timestamp": datetime.now().isoformat()
        })
        
        self.results.append(results)
        return results
        
    def _simulate_project_loading(self, size_mb: float) -> float:
        """Simulate project loading time based on file size."""
        # Simulate current performance: ~0.5-1.0 seconds per MB
        base_time = size_mb * 0.75 * 1000  # milliseconds
        variation = base_time * 0.2  # 20% variation
        import random
        return base_time + random.uniform(-variation, variation)
        
    def _simulate_vector_rendering(self, size_mb: float) -> float:
        """Simulate vector rendering time."""
        # Simulate current performance: ~850ms average for complex tiles
        base_time = 850  # milliseconds
        size_factor = min(size_mb / 10, 2.0)  # Scale with size, cap at 2x
        variation = base_time * 0.15  # 15% variation
        import random
        return (base_time * size_factor) + random.uniform(-variation, variation)
        
    def compare_with_baseline(self, baseline_file: str, improvement_threshold: float) -> Dict[str, Any]:
        """Compare current results with baseline measurements."""
        if not self.results:
            raise ValueError("No benchmark results to compare")
            
        try:
            with open(baseline_file, 'r') as f:
                baseline_data = json.load(f)
        except FileNotFoundError:
            print(f"Baseline file not found: {baseline_file}")
            return {"comparison": "no_baseline", "results": self.results}
            
        comparison_results = {
            "baseline_file": baseline_file,
            "improvement_threshold": improvement_threshold,
            "comparisons": [],
            "overall_improvement": False
        }
        
        for current_result in self.results:
            benchmark_type = current_result["benchmark_type"]
            
            # Find matching baseline result
            baseline_result = None
            for baseline in baseline_data.get("results", []):
                if baseline.get("benchmark_type") == benchmark_type:
                    baseline_result = baseline
                    break
                    
            if not baseline_result:
                continue
                
            current_avg = current_result["timing_stats"]["avg"]
            baseline_avg = baseline_result["timing_stats"]["avg"]
            
            improvement_percent = ((baseline_avg - current_avg) / baseline_avg) * 100
            meets_threshold = improvement_percent >= improvement_threshold
            
            comparison = {
                "benchmark_type": benchmark_type,
                "current_avg_ms": current_avg,
                "baseline_avg_ms": baseline_avg,
                "improvement_percent": improvement_percent,
                "meets_threshold": meets_threshold,
                "threshold": improvement_threshold
            }
            
            comparison_results["comparisons"].append(comparison)
            
        # Check if overall improvement targets are met
        project_loading_ok = False
        vector_rendering_ok = False
        
        for comp in comparison_results["comparisons"]:
            if comp["benchmark_type"] == "project_loading" and comp["improvement_percent"] >= 30.0:
                project_loading_ok = True
            elif comp["benchmark_type"] == "vector_rendering" and comp["improvement_percent"] >= 5.0:
                vector_rendering_ok = True
                
        comparison_results["overall_improvement"] = project_loading_ok and vector_rendering_ok
        comparison_results["project_loading_target_met"] = project_loading_ok
        comparison_results["vector_rendering_target_met"] = vector_rendering_ok
        
        return comparison_results
        
    def save_results(self, filename: str = None) -> str:
        """Save benchmark results to JSON file."""
        if filename is None:
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            filename = f"benchmark_results_{timestamp}.json"
            
        output_file = self.output_dir / filename
        
        output_data = {
            "metadata": {
                "generated_at": datetime.now().isoformat(),
                "tool_version": "1.0.0",
                "system_info": {
                    "platform": sys.platform,
                    "python_version": sys.version,
                    "cpu_count": psutil.cpu_count(),
                    "memory_gb": psutil.virtual_memory().total / (1024**3)
                }
            },
            "results": self.results
        }
        
        with open(output_file, 'w') as f:
            json.dump(output_data, f, indent=2)
            
        print(f"Results saved to: {output_file}")
        return str(output_file)
        
    def generate_report(self, output_file: str = None) -> str:
        """Generate human-readable performance report."""
        if output_file is None:
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            output_file = str(self.output_dir / f"performance_report_{timestamp}.md")
            
        with open(output_file, 'w') as f:
            f.write("# QGIS Performance Benchmark Report\n\n")
            f.write(f"Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n\n")
            
            for result in self.results:
                f.write(f"## {result['benchmark_type'].replace('_', ' ').title()}\n\n")
                
                if result["benchmark_type"] == "project_loading":
                    f.write(f"- **Project**: {result['project_path']}\n")
                    f.write(f"- **Size**: {result['project_size_mb']:.1f} MB\n")
                elif result["benchmark_type"] == "vector_rendering":
                    f.write(f"- **FGB File**: {result['fgb_path']}\n")
                    f.write(f"- **Size**: {result['fgb_size_mb']:.1f} MB\n")
                    
                f.write(f"- **Iterations**: {result['iterations']}\n\n")
                
                stats = result["timing_stats"]
                f.write("### Timing Results\n\n")
                f.write(f"- Average: {stats['avg']:.1f} ms\n")
                f.write(f"- Minimum: {stats['min']:.1f} ms\n")
                f.write(f"- Maximum: {stats['max']:.1f} ms\n")
                f.write(f"- Total: {stats['total']:.1f} ms\n\n")
                
                mem_stats = result["memory_stats"]
                f.write("### Memory Usage\n\n")
                f.write(f"- Baseline: {mem_stats['baseline']:.1f} MB\n")
                f.write(f"- Peak: {mem_stats['peak']:.1f} MB\n")
                f.write(f"- Max Delta: {mem_stats['max_delta']:.1f} MB\n\n")
                
        print(f"Report generated: {output_file}")
        return output_file


def main():
    """Command-line interface for performance measurement tools."""
    import argparse
    
    parser = argparse.ArgumentParser(description="QGIS Performance Measurement Tools")
    parser.add_argument("--output-dir", default="./benchmark_results",
                       help="Output directory for results")
    parser.add_argument("--project-file", help="QGIS project file to benchmark")
    parser.add_argument("--fgb-file", help="FlatGeobuf file to benchmark")
    parser.add_argument("--iterations", type=int, default=3,
                       help="Number of iterations for each benchmark")
    parser.add_argument("--baseline", help="Baseline results file for comparison")
    parser.add_argument("--report", action="store_true",
                       help="Generate human-readable report")
    
    args = parser.parse_args()
    
    runner = BenchmarkRunner(args.output_dir)
    
    if args.project_file:
        runner.run_project_loading_benchmark(args.project_file, args.iterations)
        
    if args.fgb_file:
        runner.run_vector_rendering_benchmark(args.fgb_file, args.iterations)
        
    if runner.results:
        results_file = runner.save_results()
        
        if args.report:
            runner.generate_report()
            
        if args.baseline:
            comparison = runner.compare_with_baseline(args.baseline, 5.0)
            print("\nPerformance Comparison:")
            for comp in comparison["comparisons"]:
                print(f"  {comp['benchmark_type']}: {comp['improvement_percent']:.1f}% improvement")
                
    else:
        print("No benchmarks run. Specify --project-file or --fgb-file")


if __name__ == "__main__":
    main()