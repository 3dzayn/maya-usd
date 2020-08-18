#!/pxrpythonsubst
#
# Copyright 2020 Autodesk
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

from pxr import Plug
from pxr import Usd
from pxr import UsdShade

import os
import unittest

from maya import cmds
from maya import standalone

import fixturesUtils

class testUsdExportCustomRenderContext(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.input_path = fixturesUtils.setUpClass(__file__)

        test_dir = os.path.join(cls.input_path,
                                "UsdExportCustomRenderContextTest")

        cmds.workspace(test_dir, o=True)

    @classmethod
    def tearDownClass(cls):
        standalone.uninitialize()

    def testUsdExportCustomRenderContext(self):
        """
        Tests a custom exporter for a render context that exists in an unloaded
        plugin.
        """
        plugin_path = os.path.join(self.input_path, "..", "plugin")
        Plug.Registry().RegisterPlugins(plugin_path)

        # Load test scene:
        file_path = os.path.join(self.input_path,
                                 "UsdExportCustomRenderContextTest",
                                 "testScene.ma")
        cmds.file(file_path, force=True, open=True)

        # Export to USD:
        usd_path = os.path.abspath('UsdExportCustomRenderContextTest.usda')

        # Using the "maya" render context, which only exists in the test plugin
        options = ["shadingMode=useRegistry",
                   "renderContext=maya",
                   "mergeTransformAndShape=1"]

        default_ext_setting = cmds.file(q=True, defaultExtensions=True)
        cmds.file(defaultExtensions=False)
        cmds.file(usd_path, force=True,
                  options=";".join(options),
                  typ="USD Export", pr=True, ea=True)
        cmds.file(defaultExtensions=default_ext_setting)

        # Make sure we have a Maya standardSurface material in the USD file:
        stage = Usd.Stage.Open(usd_path)
        material = stage.GetPrimAtPath(
            "/pCube1/Looks/standardSurface2SG/standardSurface2")
        shader = UsdShade.Shader(material)
        self.assertEqual(shader.GetIdAttr().Get(), "standardSurface")


if __name__ == '__main__':
    unittest.main(verbosity=2)