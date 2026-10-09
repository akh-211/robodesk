import unittest

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec

from tools.ota_signing import signing_message


class OtaSigningContextTests(unittest.TestCase):
    def test_dual_board_signatures_cannot_cross_roles_or_standalone(self):
        private_key = ec.generate_private_key(ec.SECP256R1())
        public_key = private_key.public_key()
        targets = ("ESP32-S3", "ESP32-C3", "ESP32-S3/robot-link-v1", "ESP32-C3/gateway-link-v1")
        for target in targets:
            message = signing_message(target, 43, 123456, "b" * 64)
            signature = private_key.sign(message, ec.ECDSA(hashes.SHA256()))
            public_key.verify(signature, message, ec.ECDSA(hashes.SHA256()))
            for other in targets:
                if other != target:
                    with self.assertRaises(InvalidSignature):
                        public_key.verify(signature, signing_message(other, 43, 123456, "b" * 64), ec.ECDSA(hashes.SHA256()))

    def test_signature_is_bound_to_target_board(self):
        private_key = ec.generate_private_key(ec.SECP256R1())
        public_key = private_key.public_key()
        image_hash = "a" * 64
        s3_message = signing_message("ESP32-S3", 42, 123456, image_hash)
        c3_message = signing_message("ESP32-C3", 42, 123456, image_hash)
        signature = private_key.sign(s3_message, ec.ECDSA(hashes.SHA256()))

        public_key.verify(signature, s3_message, ec.ECDSA(hashes.SHA256()))
        with self.assertRaises(InvalidSignature):
            public_key.verify(signature, c3_message, ec.ECDSA(hashes.SHA256()))


if __name__ == "__main__":
    unittest.main()
