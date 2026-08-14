# pragma version 0.4.3
# pragma optimize gas

N_COINS: constant(uint256) = 2
FEE_PRECISION: constant(uint256) = 10**10
BPS_SCALE: constant(uint256) = 10_000
MIN_SEQUENCE_FEE_BPS: constant(uint256) = 1
MAX_SEQUENCE_FEE_BPS: constant(uint256) = 1_000
SEQUENCE_CYCLE_LENGTH: constant(uint256) = 100
SEQUENCE_HALF_CYCLE: constant(uint256) = 50

POOL: public(immutable(address))
policy_fee: public(uint256)
update_nonce: public(uint256)
last_price_oracle: public(uint256)
last_oracle_timestamp: public(uint256)


@deploy
def __init__(pool: address):
    POOL = pool


@external
@view
def get_fee(xp: uint256[N_COINS]) -> uint256:
    return self.policy_fee


@external
@view
def get_price_scale() -> uint256:
    if self.last_oracle_timestamp == 0:
        return 0
    return 2 * self.last_price_oracle


@external
def update_pool_state(
    xp: uint256[N_COINS],
    price_scale: uint256,
    price_oracle: uint256,
    last_prices: uint256,
    virtual_price: uint256,
    xcp_profit: uint256,
    D: uint256,
    oracle_timestamp: uint256,
):
    assert msg.sender == POOL, "auth!"

    self.last_price_oracle = price_oracle
    self.last_oracle_timestamp = oracle_timestamp
    self.update_nonce += 1
    idx: uint256 = (self.update_nonce - 1) % SEQUENCE_CYCLE_LENGTH
    leg: uint256 = idx
    if idx >= SEQUENCE_HALF_CYCLE:
        leg = SEQUENCE_CYCLE_LENGTH - 1 - idx
    fee_bps: uint256 = MIN_SEQUENCE_FEE_BPS + (
        leg * (MAX_SEQUENCE_FEE_BPS - MIN_SEQUENCE_FEE_BPS)
    ) // (SEQUENCE_HALF_CYCLE - 1)
    self.policy_fee = fee_bps * FEE_PRECISION // BPS_SCALE
