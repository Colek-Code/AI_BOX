"""Isolated regression tests for public credential projection; no real credentials."""
import hashlib
import unittest
from public_credentials import PublicationError, excluded, sanitize, scan, redact_repeated_literals

def policy(rules):
    return {'version':1,'exclude':['project_doc/*','cpu/wl82/tools/app.bin','*.key'],
            'slots':{'fixture.c':rules},'noncredential_literals':{}}

class CredentialTests(unittest.TestCase):
    def test_public_projection_retains_local_bytes(self):
        raw=b'#define D_WIFI_PWD "local-fixture-password"\r\nint x;\r\n'
        cfg=policy([{'kind':'define','symbol':'D_WIFI_PWD','count':1}])
        clean,report,values=sanitize('fixture.c',raw,cfg)
        self.assertEqual(clean,b'#define D_WIFI_PWD ""\r\nint x;\r\n')
        self.assertIn(b'local-fixture-password',raw)
        self.assertEqual(report[0]['symbol'],'D_WIFI_PWD')
        self.assertNotIn('local-fixture-password',str(report))
        self.assertEqual(scan('fixture.c',clean,cfg),[])

    def test_split_literals_and_comments_are_fully_removed(self):
        raw=b'#define API_KEY "first-part" /* separator */ "second-part"\n'
        clean,_,_=sanitize('fixture.c',raw,policy([{'kind':'define','symbol':'API_KEY','count':1}]))
        self.assertEqual(clean,b'#define API_KEY ""\n')

    def test_declaration_drift_fails_closed(self):
        with self.assertRaises(PublicationError):
            sanitize('fixture.c',b'int x;',policy([{'kind':'define','symbol':'API_KEY','count':1}]))

    def test_protocol_field_exception_is_value_bound(self):
        cfg=policy([])
        cfg['noncredential_literals']={'fixture.c':{'MSG_PASSWORD':[hashlib.sha256(b'"password"').hexdigest()]}}
        self.assertFalse(scan('fixture.c',b'#define MSG_PASSWORD "password"\n',cfg))
        self.assertTrue(scan('fixture.c',b'#define MSG_PASSWORD "new-private-value"\n',cfg))

    def test_unknown_secret_declaration_is_rejected(self):
        self.assertTrue(scan('fixture.c',b'const char *device_secret = "fixture-value-123";',policy([])))

    def test_assignment_text_inside_log_string_is_not_a_declaration(self):
        self.assertFalse(scan('fixture.c',b'log("password = \\"x\\" ");',policy([])))

    def test_json_token_is_checked(self):
        self.assertTrue(scan('fixture.json',b'{"token":"fixture-value-123"}',policy([])))

    def test_token_shape_has_no_value_in_diagnostic(self):
        token=b'ghp_'+b'Z'*36
        findings=scan('readme.md',token,policy([]))
        self.assertTrue(findings)
        self.assertNotIn(token.decode(),str(findings))

    def test_known_repeated_value_is_removed_from_text(self):
        value=b'isolated-repeat-fixture-value'
        self.assertNotIn(value,redact_repeated_literals(b'comment '+value,{value}))

    def test_generated_firmware_and_keys_are_excluded(self):
        cfg=policy([])
        self.assertTrue(excluded('cpu/wl82/tools/app.bin',cfg))
        self.assertTrue(excluded('cpu/wl82/tools/board.key',cfg))
        self.assertFalse(excluded('cpu/wl82/liba/sdk.a',cfg))

    def test_public_placeholder_is_idempotent(self):
        cfg=policy([{'kind':'define','symbol':'AP_PWD','count':1,'public_value':'CHANGE_ME'}])
        raw=b'#define AP_PWD "local-fixture"\n'
        clean,_,_=sanitize('fixture.c',raw,cfg)
        self.assertEqual(sanitize('fixture.c',clean,cfg)[0],clean)
        self.assertFalse(scan('fixture.c',clean,cfg))

    def test_literal_wifi_arguments_are_sanitized(self):
        raw=b'wifi_enter_sta_mode("fixture-network", "fixture-password");'
        cfg=policy([{'kind':kind,'symbol':'wifi_enter_sta_mode','count':1}
                    for kind in ('wifi_ssid_call','wifi_password_call')])
        clean,_,_=sanitize('fixture.c',raw,cfg)
        self.assertEqual(clean,b'wifi_enter_sta_mode("", "");')
        self.assertFalse(scan('fixture.c',clean,cfg))

    def test_new_literal_wifi_password_is_blocked(self):
        self.assertTrue(scan('fixture.c',b'wifi_enter_sta_mode(config.ssid, "fixture-password");',policy([])))

    def test_new_app_access_and_private_key_fields_are_blocked(self):
        for name in ('APP_KEY','ACCESS_KEY','AUTH_KEY','PRIVATE_KEY','DEVICE_KEY'):
            self.assertTrue(scan('fixture.c',('#define '+name+' "fixture-value"').encode(),policy([])))

    def test_plain_ini_password_is_blocked(self):
        self.assertTrue(scan('fixture.ini',b'password = fixture-value\n',policy([])))
        self.assertFalse(scan('fixture.ini',b'password = ${LOCAL_PASSWORD}\n',policy([])))

    def test_private_key_payload_is_blocked(self):
        data=b'-----BEGIN PRIVATE KEY-----\n'+b'A'*100+b'\n-----END PRIVATE KEY-----'
        self.assertTrue(scan('fixture.pem',data,policy([])))

    def test_key_extension_exclusion_is_case_insensitive(self):
        self.assertTrue(excluded('cpu/wl82/tools/private.KEY',policy([])))

    def test_private_pem_removed_public_certificate_preserved(self):
        cert=b'-----BEGIN CERTIFICATE-----\npublic-fixture\n-----END CERTIFICATE-----\n'
        raw=cert+b'-----BEGIN PRIVATE KEY-----\n'+b'A'*100+b'\n-----END PRIVATE KEY-----'
        cfg=policy([]);cfg['strip_private_pem']=['fixture.pem']
        clean,report,_=sanitize('fixture.pem',raw,cfg)
        self.assertEqual(clean,cert)
        self.assertEqual(report[0]['action'],'remove_private_pem_block')

if __name__=='__main__':unittest.main(verbosity=2)
