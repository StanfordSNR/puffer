import string
import random


def random_token():
    return ''.join(random.choice(string.ascii_letters + string.digits)
                   for _ in range(16))
