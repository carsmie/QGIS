"""QGIS Unit tests for the QGIS_SERVER_WMS_PNG_COMPRESSION_LEVEL server option.

From build dir, run: ctest -R PyQgsServerWMSPngCompression -V

.. note:: This test needs env vars to be set before the server is
          configured for the first time, for this
          reason it cannot run as a test case of another server
          test.

.. note:: This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

"""

import os

# Needed on Qt 5 so that the serialization of XML is consistent among all
# executions
os.environ["QT_HASH_SEED"] = "1"

import urllib.parse

from qgis.PyQt.QtGui import QImage
from qgis.testing import unittest
from test_qgsserver import QgsServerTestBase


class TestQgsServerWMSPngCompression(QgsServerTestBase):
    """QGIS Server WMS Tests for PNG output written with a custom zlib compression level"""

    # Set to True to re-generate reference files for this class
    regenerate_reference = False

    @classmethod
    def setUpClass(self):
        os.environ["QGIS_SERVER_WMS_PNG_COMPRESSION_LEVEL"] = "1"
        super().setUpClass()

    @classmethod
    def tearDownClass(self):
        os.environ.pop("QGIS_SERVER_WMS_PNG_COMPRESSION_LEVEL")
        super().tearDownClass()

    def get_map(self, format):
        qs = "?" + "&".join(
            [
                "%s=%s" % i
                for i in list(
                    {
                        "MAP": urllib.parse.quote(self.projectPath),
                        "SERVICE": "WMS",
                        "VERSION": "1.1.1",
                        "REQUEST": "GetMap",
                        "LAYERS": "Country",
                        "STYLES": "",
                        "FORMAT": format,
                        "BBOX": "-16817707,-4710778,5696513,14587125",
                        "HEIGHT": "500",
                        "WIDTH": "500",
                        "CRS": "EPSG:3857",
                    }.items()
                )
            ]
        )
        return self._result(self._execute_request(qs))

    def test_wms_getmap_png(self):
        r, h = self.get_map("image/png")
        self.assertEqual(h.get("Content-Type"), "image/png")

        image = QImage.fromData(r, "PNG")
        self.assertFalse(image.isNull())
        self.assertEqual(image.size().width(), 500)
        self.assertTrue(image.hasAlphaChannel())

        # the same control image as for the default Qt PNG writer
        self._img_diff_error(r, h, "WMS_GetMap_Basic")

    def test_wms_getmap_png_mode_unchanged(self):
        # 8 bit PNG output keeps using the Qt PNG writer
        r, h = self.get_map("image/png; mode=8bit")
        image = QImage.fromData(r, "PNG")
        self.assertFalse(image.isNull())
        self.assertEqual(image.format(), QImage.Format.Format_Indexed8)


if __name__ == "__main__":
    unittest.main()
