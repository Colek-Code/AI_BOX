"""Small fixtures only; no SDK, compiler, Make or device invocation."""
from pathlib import Path
import tempfile
import unittest
import runpy
import sys
from unittest.mock import patch
from d_code_checks.include_contract import inspect, make_search_roots

class Includes(unittest.TestCase):
    def setUp(self):
        temporary=Path(__file__).resolve().parents[2]/'build/out/architecture-contract/temp'
        temporary.mkdir(parents=True,exist_ok=True)
        self.temp=tempfile.TemporaryDirectory(dir=temporary);self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)/'project';self.root.mkdir()
        self.source=self.put('modules/services/network/example.c','')
        self.target=self.put('modules/applications/calendar/internal.h','')

    def put(self,path,text):
        p=self.root/path;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(text,encoding='utf-8');return p

    def scan(self,text,roots=None):
        self.source.write_text(text,encoding='utf-8')
        with patch('subprocess.run',side_effect=AssertionError('No subprocess allowed')):
            return inspect(self.root,roots or [self.root],files=[self.source])

    def test_forbidden_owner_is_independent_of_spelling(self):
        for path in ('modules/applications/calendar/internal.h','../../applications/calendar/internal.h','..\\..\\applications\\calendar\\internal.h'):
            row=self.scan('#include "'+path+'"')['findings'][0]
            self.assertEqual(row['status'],'violation');self.assertEqual(row['reason'],'service_to_product_or_integration')

    def test_same_directory_and_qualified_public_service(self):
        self.put('modules/services/network/local.h','');self.put('modules/services/clock/clock.h','')
        r=self.scan('#include "local.h"\n#include "modules/services/clock/clock.h"')
        self.assertEqual(r['counts']['resolved'],2)

    def test_private_other_owner(self):
        self.put('modules/services/clock/internal/state.h','')
        self.assertEqual(self.scan('#include "modules/services/clock/internal/state.h"')['findings'][0]['reason'],'private_owner_boundary')

    def test_comments_and_condition_locations(self):
        r=self.scan('/* #include "bad.h" */\n// #include "bad2.h"\n#if 0\n#include "modules/applications/calendar/internal.h"\n#endif')
        self.assertEqual(len(r['findings']),1);row=r['findings'][0]
        self.assertEqual(row['line'],4);self.assertEqual(row['activation'],'not_evaluated');self.assertTrue(row['conditions'])

    def test_macros_unknown_project_header_and_no_unique_name_guess(self):
        self.put('modules/services/other/unique.h','')
        for text in ('#include HEADER_MACRO','#include "unique.h"','#include "missing.h"'):
            self.assertEqual(self.scan(text)['exit_code'],2)

    def test_duplicate_names_use_ordered_roots_not_global_guess(self):
        self.put('modules/services/network/local.h','');self.put('modules/services/other/local.h','')
        row=self.scan('#include "local.h"')['findings'][0]
        self.assertEqual(row['target'],'modules/services/network/local.h')

    def test_external_sdk_and_standard_headers(self):
        sdk=Path(self.temp.name)/'sdk';sdk.mkdir();(sdk/'api.h').write_text('')
        r=self.scan('#include <stdint.h>\n#include "api.h"',[self.root,sdk])
        self.assertEqual(r['counts']['external'],2)

    def test_escape_and_short_cross_owner(self):
        outside=Path(self.temp.name)/'outside.h';outside.write_text('')
        self.assertEqual(self.scan('#include "../../../../outside.h"')['exit_code'],2)
        other=self.put('modules/services/other/shared.h','')
        self.assertEqual(self.scan('#include "shared.h"',[self.root,other.parent])['findings'][0]['reason'],'cross_directory_short_include')

    def test_make_reader_does_not_execute_recipes(self):
        make=self.put('board/Makefile','ROOT_PREFIX := ../..\nINCLUDES := \\\n -I../../project \\\n -I$(UNKNOWN)/inc\nall:\n\tisd_download.exe\n')
        with patch('subprocess.run',side_effect=AssertionError('No Make invocation')):
            roots,unknown=make_search_roots(make)
        self.assertTrue(roots);self.assertEqual(unknown,['$(UNKNOWN)/inc'])

    def test_declared_vendor_sdk_is_not_a_product_interface(self):
        sdk=self.root/'modules/integrations/voice/sdk/include'
        sdk.mkdir(parents=True);(sdk/'json.h').write_text('')
        for value in ('json.h','modules/integrations/voice/sdk/include/json.h'):
            row=self.scan('#include "'+value+'"',[self.root,sdk])['findings'][0]
            self.assertEqual(row['status'],'external');self.assertEqual(row['reason'],'declared_vendor_sdk_header')

    def test_make_quotes_comments_and_later_reassignment_do_not_hide_roots(self):
        make=self.put('board/Makefile',"# The tool's include roots\nINCLUDES := \\\n -I../ \\\n -I../config\nINCLUDES := $(subst root,vendor,$(INCLUDES))\n")
        roots,unknown=make_search_roots(make)
        self.assertEqual(roots,[self.root.resolve(),(self.root/'config').resolve()]);self.assertEqual(unknown,[])

    def test_cli_architecture_group_cannot_enter_build_checks(self):
        import d_code_checks.support as support
        make=self.put('board/Makefile','INCLUDES := -I../\nall:\n\tisd_download.exe\n')
        namespace=runpy.run_path(str(Path(__file__).resolve().parents[1]/'check_config_menu.py'))
        args=['check_config_menu.py','--group','architecture','--work-dir',str(self.root/'report')]
        with patch.object(sys,'argv',args),patch.object(support,'D_CODE',self.root),patch.object(support,'MAKEFILE',make),patch('subprocess.run',side_effect=AssertionError('No build allowed')):
            self.assertEqual(namespace['main'](),0)

if __name__=='__main__':unittest.main()
