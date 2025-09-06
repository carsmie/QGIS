#!/usr/bin/env python3
"""
QGIS Performance Test Data Generator

This script generates test datasets for performance benchmarking:
- Large QGIS project files (~100MB) for project loading tests
- FlatGeobuf (.fgb) files with varying complexity for vector rendering tests
- Sample data with different layer counts, styling complexity, and geometries

Usage:
    python generate_test_datasets.py [--output-dir PATH] [--project-size SIZE]
"""

import argparse
import json
import os
import random
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Dict, List, Optional

try:
    import numpy as np
    from osgeo import gdal, ogr, osr
except ImportError as e:
    print(f"Error: Required dependencies not found: {e}")
    print("Please install GDAL Python bindings: pip install gdal")
    sys.exit(1)


class TestDataGenerator:
    """Generates various test datasets for QGIS performance testing."""
    
    def __init__(self, output_dir: str):
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(parents=True, exist_ok=True)
        
    def generate_large_project(self, target_size_mb: int = 100) -> str:
        """Generate a large QGIS project file for testing project loading performance."""
        print(f"Generating large QGIS project (~{target_size_mb}MB)...")
        
        project_file = self.output_dir / f"large_project_{target_size_mb}mb.qgs"
        
        # Create XML structure for QGIS project
        root = ET.Element("qgis", version="3.34.0", projectname="Performance Test Project")
        
        # Add project metadata
        metadata = ET.SubElement(root, "metadata")
        ET.SubElement(metadata, "identifier").text = "performance_test_project"
        ET.SubElement(metadata, "title").text = f"Performance Test Project ({target_size_mb}MB)"
        ET.SubElement(metadata, "abstract").text = "Large project file for performance testing"
        
        # Calculate number of layers needed to reach target size
        base_layer_size_kb = 500  # Approximate size per layer in KB
        target_layers = (target_size_mb * 1024) // base_layer_size_kb
        
        # Add layers
        projectlayers = ET.SubElement(root, "projectlayers")
        
        for i in range(target_layers):
            layer = self._create_test_layer(i, projectlayers)
            
        # Add layer tree
        layertree = ET.SubElement(root, "layer-tree-group")
        ET.SubElement(layertree, "name").text = "Root"
        ET.SubElement(layertree, "expanded").text = "1"
        
        for i in range(target_layers):
            layer_item = ET.SubElement(layertree, "layer-tree-layer")
            ET.SubElement(layer_item, "name").text = f"test_layer_{i:04d}"
            ET.SubElement(layer_item, "id").text = f"layer_{i}"
            ET.SubElement(layer_item, "expanded").text = "1"
            ET.SubElement(layer_item, "checked").text = "Qt::Checked"
            
        # Add map canvas settings
        mapcanvas = ET.SubElement(root, "mapcanvas")
        ET.SubElement(mapcanvas, "units").text = "meters"
        
        extent = ET.SubElement(mapcanvas, "extent")
        ET.SubElement(extent, "xmin").text = "-20037508"
        ET.SubElement(extent, "ymin").text = "-20037508"
        ET.SubElement(extent, "xmax").text = "20037508"
        ET.SubElement(extent, "ymax").text = "20037508"
        
        # Write to file
        tree = ET.ElementTree(root)
        ET.indent(tree, space="  ", level=0)
        tree.write(project_file, encoding="utf-8", xml_declaration=True)
        
        # Verify file size
        actual_size = project_file.stat().st_size / (1024 * 1024)
        print(f"Generated project file: {project_file} ({actual_size:.1f}MB)")
        
        return str(project_file)
        
    def _create_test_layer(self, layer_id: int, parent: ET.Element) -> ET.Element:
        """Create a test layer with complex styling."""
        maplayer = ET.SubElement(parent, "maplayer")
        maplayer.set("id", f"layer_{layer_id}")
        maplayer.set("type", "vector")
        maplayer.set("geometry", random.choice(["Point", "LineString", "Polygon"]))
        
        # Layer properties
        ET.SubElement(maplayer, "layername").text = f"test_layer_{layer_id:04d}"
        ET.SubElement(maplayer, "srs").text = "EPSG:4326"
        
        # Data source (use memory provider for testing)
        datasource = f"memory?geometry={maplayer.get('geometry')}&crs=epsg:4326&index=yes"
        ET.SubElement(maplayer, "datasource").text = datasource
        
        # Add complex styling to increase file size
        renderer = ET.SubElement(maplayer, "renderer-v2")
        renderer.set("type", "categorizedSymbol")
        
        # Add multiple categories with different styles
        for cat_id in range(20):  # 20 categories per layer
            category = ET.SubElement(renderer, "category")
            ET.SubElement(category, "value").text = f"category_{cat_id}"
            ET.SubElement(category, "label").text = f"Category {cat_id}"
            
            # Complex symbol definition
            symbol = ET.SubElement(category, "symbol")
            symbol.set("type", "fill")
            
            for prop_id in range(10):  # Multiple properties per symbol
                prop = ET.SubElement(symbol, "property")
                prop.set("key", f"property_{prop_id}")
                prop.set("value", f"value_{random.randint(0, 255)}")
                
        return maplayer
        
    def generate_fgb_test_files(self) -> List[str]:
        """Generate FlatGeobuf test files with varying complexity."""
        print("Generating FlatGeobuf test files...")
        
        fgb_files = []
        
        # Test configurations: (name, geometry_count, complexity)
        configs = [
            ("small_simple", 1000, "simple"),
            ("medium_complex", 10000, "complex"),
            ("large_simple", 50000, "simple"),
            ("large_complex", 25000, "complex"),
        ]
        
        for name, geom_count, complexity in configs:
            fgb_file = self._generate_fgb_file(name, geom_count, complexity)
            fgb_files.append(fgb_file)
            
        return fgb_files
        
    def _generate_fgb_file(self, name: str, geom_count: int, complexity: str) -> str:
        """Generate a single FlatGeobuf file."""
        fgb_file = self.output_dir / f"{name}.fgb"
        
        # Create FlatGeobuf file
        driver = ogr.GetDriverByName("FlatGeobuf")
        datasource = driver.CreateDataSource(str(fgb_file))
        
        # Create spatial reference
        srs = osr.SpatialReference()
        srs.ImportFromEPSG(4326)
        
        # Create layer
        layer = datasource.CreateLayer(name, srs, ogr.wkbPolygon)
        
        # Add fields
        fields = [
            ("id", ogr.OFTInteger),
            ("name", ogr.OFTString),
            ("category", ogr.OFTString),
            ("value", ogr.OFTReal),
        ]
        
        if complexity == "complex":
            # Add more fields for complex datasets
            for i in range(20):
                fields.append((f"attr_{i}", ogr.OFTString))
                
        for field_name, field_type in fields:
            field_def = ogr.FieldDefn(field_name, field_type)
            layer.CreateField(field_def)
            
        # Generate geometries
        for i in range(geom_count):
            feature = ogr.Feature(layer.GetLayerDefn())
            
            # Set attributes
            feature.SetField("id", i)
            feature.SetField("name", f"feature_{i}")
            feature.SetField("category", f"cat_{i % 10}")
            feature.SetField("value", random.uniform(0, 1000))
            
            if complexity == "complex":
                for j in range(20):
                    feature.SetField(f"attr_{j}", f"value_{random.randint(0, 1000)}")
                    
            # Create geometry
            if complexity == "simple":
                geom = self._create_simple_polygon(i)
            else:
                geom = self._create_complex_polygon(i)
                
            feature.SetGeometry(geom)
            layer.CreateFeature(feature)
            
        datasource = None  # Close file
        
        file_size = fgb_file.stat().st_size / (1024 * 1024)
        print(f"Generated FGB file: {fgb_file} ({file_size:.1f}MB, {geom_count} features)")
        
        return str(fgb_file)
        
    def _create_simple_polygon(self, seed: int) -> ogr.Geometry:
        """Create a simple rectangular polygon."""
        random.seed(seed)
        
        # Random location
        center_x = random.uniform(-180, 180)
        center_y = random.uniform(-85, 85)
        size = random.uniform(0.01, 0.1)
        
        # Create rectangle
        ring = ogr.Geometry(ogr.wkbLinearRing)
        ring.AddPoint(center_x - size, center_y - size)
        ring.AddPoint(center_x + size, center_y - size)
        ring.AddPoint(center_x + size, center_y + size)
        ring.AddPoint(center_x - size, center_y + size)
        ring.AddPoint(center_x - size, center_y - size)
        
        polygon = ogr.Geometry(ogr.wkbPolygon)
        polygon.AddGeometry(ring)
        
        return polygon
        
    def _create_complex_polygon(self, seed: int) -> ogr.Geometry:
        """Create a complex polygon with multiple rings."""
        random.seed(seed)
        
        # Random location
        center_x = random.uniform(-180, 180)
        center_y = random.uniform(-85, 85)
        
        # Create outer ring with many points
        outer_ring = ogr.Geometry(ogr.wkbLinearRing)
        num_points = random.randint(50, 200)
        
        for i in range(num_points):
            angle = (2 * np.pi * i) / num_points
            radius = random.uniform(0.05, 0.2)
            x = center_x + radius * np.cos(angle)
            y = center_y + radius * np.sin(angle)
            outer_ring.AddPoint(x, y)
            
        outer_ring.AddPoint(outer_ring.GetX(0), outer_ring.GetY(0))  # Close ring
        
        polygon = ogr.Geometry(ogr.wkbPolygon)
        polygon.AddGeometry(outer_ring)
        
        # Add holes for extra complexity
        num_holes = random.randint(1, 5)
        for hole_id in range(num_holes):
            hole_ring = ogr.Geometry(ogr.wkbLinearRing)
            hole_size = random.uniform(0.01, 0.05)
            hole_x = center_x + random.uniform(-0.1, 0.1)
            hole_y = center_y + random.uniform(-0.1, 0.1)
            
            hole_ring.AddPoint(hole_x - hole_size, hole_y - hole_size)
            hole_ring.AddPoint(hole_x + hole_size, hole_y - hole_size)
            hole_ring.AddPoint(hole_x + hole_size, hole_y + hole_size)
            hole_ring.AddPoint(hole_x - hole_size, hole_y + hole_size)
            hole_ring.AddPoint(hole_x - hole_size, hole_y - hole_size)
            
            polygon.AddGeometry(hole_ring)
            
        return polygon
        
    def generate_benchmark_metadata(self, project_files: List[str], fgb_files: List[str]) -> str:
        """Generate metadata file describing all test datasets."""
        metadata_file = self.output_dir / "benchmark_metadata.json"
        
        metadata = {
            "description": "QGIS Performance Test Datasets",
            "generated_by": "generate_test_datasets.py",
            "project_files": [],
            "fgb_files": [],
        }
        
        for project_file in project_files:
            path = Path(project_file)
            size_mb = path.stat().st_size / (1024 * 1024)
            metadata["project_files"].append({
                "path": str(path),
                "size_mb": round(size_mb, 1),
                "description": f"Large QGIS project for testing project loading performance"
            })
            
        for fgb_file in fgb_files:
            path = Path(fgb_file)
            size_mb = path.stat().st_size / (1024 * 1024)
            
            # Extract info from filename
            name = path.stem
            if "simple" in name:
                complexity = "simple"
            else:
                complexity = "complex"
                
            metadata["fgb_files"].append({
                "path": str(path),
                "size_mb": round(size_mb, 1),
                "complexity": complexity,
                "description": f"FlatGeobuf file for vector rendering performance tests"
            })
            
        with open(metadata_file, 'w') as f:
            json.dump(metadata, f, indent=2)
            
        print(f"Generated metadata file: {metadata_file}")
        return str(metadata_file)


def main():
    parser = argparse.ArgumentParser(description="Generate QGIS performance test datasets")
    parser.add_argument("--output-dir", default="./test_data", 
                       help="Output directory for test datasets")
    parser.add_argument("--project-size", type=int, default=100,
                       help="Target size for project files in MB")
    parser.add_argument("--skip-project", action="store_true",
                       help="Skip project file generation")
    parser.add_argument("--skip-fgb", action="store_true", 
                       help="Skip FGB file generation")
    
    args = parser.parse_args()
    
    generator = TestDataGenerator(args.output_dir)
    
    project_files = []
    fgb_files = []
    
    try:
        if not args.skip_project:
            project_file = generator.generate_large_project(args.project_size)
            project_files.append(project_file)
            
        if not args.skip_fgb:
            fgb_files = generator.generate_fgb_test_files()
            
        # Generate metadata
        metadata_file = generator.generate_benchmark_metadata(project_files, fgb_files)
        
        print(f"\nTest data generation complete!")
        print(f"Output directory: {args.output_dir}")
        print(f"Project files: {len(project_files)}")
        print(f"FGB files: {len(fgb_files)}")
        print(f"Metadata: {metadata_file}")
        
    except Exception as e:
        print(f"Error generating test data: {e}")
        return 1
        
    return 0


if __name__ == "__main__":
    sys.exit(main())