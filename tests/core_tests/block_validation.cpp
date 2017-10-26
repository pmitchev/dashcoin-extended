// Copyright (c) 2012-2014, The CryptoNote developers, The Bytecoin developers
//
// This file is part of Bytecoin.
//
// Bytecoin is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Bytecoin is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with Bytecoin.  If not, see <http://www.gnu.org/licenses/>.

#include "block_validation.h"
#include "TestGenerator.h"

using namespace epee;
using namespace cryptonote;

#define BLOCK_VALIDATION_INIT_GENERATE()                        \
  GENERATE_ACCOUNT(miner_account);                              \
  MAKE_GENESIS_BLOCK(events, blk_0, miner_account, 1338224400);

namespace {
  bool lift_up_difficulty(const cryptonote::Currency& currency, std::vector<test_event_entry>& events,
                          std::vector<uint64_t>& timestamps,
                          std::vector<cryptonote::difficulty_type>& cummulative_difficulties, test_generator& generator,
                          size_t new_block_count, const cryptonote::Block blk_last,
                          const cryptonote::account_base& miner_account, uint8_t block_major_version) {
    cryptonote::difficulty_type commulative_diffic = cummulative_difficulties.empty() ? 0 : cummulative_difficulties.back();
    cryptonote::Block blk_prev = blk_last;
    for (size_t i = 0; i < new_block_count; ++i) {
      cryptonote::Block blk_next;
      cryptonote::difficulty_type diffic = currency.nextDifficulty(timestamps, cummulative_difficulties);
      if (!generator.constructBlockManually(blk_next, blk_prev, miner_account,
        test_generator::bf_major_ver | test_generator::bf_timestamp | test_generator::bf_diffic, 
        block_major_version, 0, blk_prev.timestamp, crypto::hash(), diffic)) {
        return false;
      }

      commulative_diffic += diffic;
      if (timestamps.size() == currency.difficultyWindow()) {
        timestamps.erase(timestamps.begin());
        cummulative_difficulties.erase(cummulative_difficulties.begin());
      }
      timestamps.push_back(blk_next.timestamp);
      cummulative_difficulties.push_back(commulative_diffic);

      events.push_back(blk_next);
      blk_prev = blk_next;
    }

    return true;
  }

  bool getParentBlockSize(const cryptonote::Block& block, size_t& size) {
    auto serializer = cryptonote::makeParentBlockSerializer(block, false, false);
    if (!cryptonote::get_object_blobsize(serializer, size)) {
      LOG_ERROR("Failed to get size of parent block");
      return false;
    }
    return true;
  }

  bool adjustParentBlockSize(cryptonote::Block& block, size_t targetSize) {
    size_t parentBlockSize;
    if (!getParentBlockSize(block, parentBlockSize)) {
      return false;
    }

    if (parentBlockSize > targetSize) {
      LOG_ERROR("Parent block size is " << parentBlockSize << " bytes that is already greater than target size " << targetSize << " bytes");
      return false;
    }

    block.parentBlock.minerTx.extra.resize(block.parentBlock.minerTx.extra.size() + (targetSize - parentBlockSize));

    if (!getParentBlockSize(block, parentBlockSize)) {
      return false;
    }

    if (parentBlockSize > targetSize) {
      if (block.parentBlock.minerTx.extra.size() < parentBlockSize - targetSize) {
        LOG_ERROR("Failed to adjust parent block size to " << targetSize);
        return false;
      }

      block.parentBlock.minerTx.extra.resize(block.parentBlock.minerTx.extra.size() - (parentBlockSize - targetSize));

      if (!getParentBlockSize(block, parentBlockSize)) {
        return false;
      }

      if (parentBlockSize + 1 == targetSize) {
        block.timestamp = std::max(block.timestamp, UINT64_C(1)) << 7;
        if (!getParentBlockSize(block, parentBlockSize)) {
          return false;
        }
      }
    }

    if (parentBlockSize != targetSize) {
      LOG_ERROR("Failed to adjust parent block size to " << targetSize);
      return false;
    }

    return true;
  }
}


bool TestBlockMajorVersionAccepted::generate(std::vector<test_event_entry>& events) const {
  TestGenerator bg(m_currency, events);
  bg.generateBlocks(1, m_blockMajorVersion);
  DO_CALLBACK(events, "check_block_accepted");
  return true;
}

bool TestBlockMajorVersionRejected::generate(std::vector<test_event_entry>& events) const {
  TestGenerator bg(m_currency, events);
  bg.generateBlocks(1, m_blockGeneratedVersion);
  DO_CALLBACK(events, "check_block_purged");
  return true;
}

bool TestBlockBigMinorVersion::generate(std::vector<test_event_entry>& events) const {
  BLOCK_VALIDATION_INIT_GENERATE();

  cryptonote::Block blk_1;
  generator.constructBlockManually(blk_1, blk_0, miner_account,
    test_generator::bf_major_ver | test_generator::bf_minor_ver, m_blockMajorVersion, BLOCK_MINOR_VERSION_0 + 1);

  events.push_back(blk_1);

  DO_CALLBACK(events, "check_block_accepted");

  return true;
}

bool gen_block_ts_not_checked::generate(std::vector<test_event_entry>& events) const
{
  BLOCK_VALIDATION_INIT_GENERATE();

  generator.defaultMajorVersion = m_blockMajorVersion;

  REWIND_BLOCKS_N(events, blk_0r, blk_0, miner_account, m_currency.timestampCheckWindow() - 2);

  Block blk_1;
  generator.constructBlockManually(blk_1, blk_0r, miner_account, 
    test_generator::bf_major_ver | test_generator::bf_timestamp, m_blockMajorVersion, 0, blk_0.timestamp - 60 * 60);
  events.push_back(blk_1);

  DO_CALLBACK(events, "check_block_accepted");

  return true;
}

bool gen_block_ts_in_past::generate(std::vector<test_event_entry>& events) const
{
  BLOCK_VALIDATION_INIT_GENERATE();

  generator.defaultMajorVersion = m_blockMajorVersion;

  REWIND_BLOCKS_N(events, blk_0r, blk_0, miner_account, m_currency.timestampCheckWindow() - 1);

  uint64_t ts_below_median = boost::get<Block>(events[m_currency.timestampCheckWindow() / 2 - 1]).timestamp;
  Block blk_1;
  generator.constructBlockManually(blk_1, blk_0r, miner_account, 
    test_generator::bf_major_ver | test_generator::bf_timestamp, m_blockMajorVersion, 0, ts_below_median);
  events.push_back(blk_1);

  DO_CALLBACK(events, "check_block_purged");

  return true;
}

bool gen_block_ts_in_future_rejected::generate(std::vector<test_event_entry>& events) const
{
  BLOCK_VALIDATION_INIT_GENERATE();

  Block blk_1;
  generator.constructBlockManually(blk_1, blk_0, miner_account, test_generator::bf_major_ver | test_generator::bf_timestamp,
    m_blockMajorVersion, 0, time(NULL) + 60 * 60 + m_currency.blockFutureTimeLimit());
  events.push_back(blk_1);

  DO_CALLBACK(events, "check_block_purged");

  return true;
}

bool gen_block_ts_in_future_accepted::generate(std::vector<test_event_entry>& events) const
{
  BLOCK_VALIDATION_INIT_GENERATE();

  Block blk_1;
  generator.constructBlockManually(blk_1, blk_0, miner_account, test_generator::bf_major_ver | test_generator::bf_timestamp,
    m_blockMajorVersion, 0, time(NULL) - 60 + m_currency.blockFutureTimeLimit());
  events.push_back(blk_1);

  DO_CALLBACK(events, "check_block_accepted");

  return true;
}


bool gen_block_invalid_prev_id::generate(std::vector<test_event_entry>& events) const
{
  BLOCK_VALIDATION_INIT_GENERATE();

  Block blk_1;
  crypto::hash prev_id = get_block_hash(blk_0);
  reinterpret_cast<char &>(prev_id) ^= 1;
  generator.constructBlockManually(blk_1, blk_0, miner_account, 
    test_generator::bf_major_ver | test_generator::bf_prev_id, m_blockMajorVersion, 0, 0, prev_id);
  events.push_back(blk_1);

  DO_CALLBACK(events, "check_block_purged");

  return true;
}

bool gen_block_invalid_prev_id::check_block_verification_context(const cryptonote::block_verification_context& bvc, size_t event_idx, const cryptonote::Block& /*blk*/)
{
  if (1 == event_idx)
    return bvc.m_marked_as_orphaned && !bvc.m_added_to_main_chain && !bvc.m_verifivation_failed;
  else
    return !bvc.m_marked_as_orphaned && bvc.m_added_to_main_chain && !bvc.m_verifivation_failed;
}

bool gen_block_invalid_nonce::generate(std::vector<test_event_entry>& events) const
{
  BLOCK_VALIDATION_INIT_GENERATE();

  std::vector<uint64_t> timestamps;
  std::vector<difficulty_type> commulative_difficulties;
  if (!lift_up_difficulty(m_currency, events, timestamps, commulative_difficulties, generator, 2, blk_0, miner_account,
    m_blockMajorVersion)) {
    return false;
  }

  // Create invalid nonce
  difficulty_type diffic = m_currency.nextDifficulty(timestamps, commulative_difficulties);
  assert(1 < diffic);
  const Block& blk_last = boost::get<Block>(events.back());
  uint64_t timestamp = blk_last.timestamp;
  Block blk_3;
  do
  {
    ++timestamp;
    blk_3.minerTx.clear();
    if (!generator.constructBlockManually(blk_3, blk_last, miner_account,
      test_generator::bf_major_ver | test_generator::bf_diffic | test_generator::bf_timestamp, m_blockMajorVersion, 0, timestamp, crypto::hash(), diffic))
      return false;
  }
  while (0 == blk_3.nonce);
  --blk_3.nonce;
  events.push_back(blk_3);

  return true;
}

bool gen_block_no_miner_tx::generate(std::vector<test_event_entry>& events) const
{
  BLOCK_VALIDATION_INIT_GENERATE();

  Transaction miner_tx;
  miner_tx.clear();

  Block blk_1;
  generator.constructBlockManually(blk_1, blk_0, miner_account, 
    test_generator::bf_major_ver | test_generator::bf_miner_tx, m_blockMajorVersion, 0, 0, crypto::hash(), 0, miner_tx);
  events.push_back(blk_1);

  DO_CALLBACK(events, "check_block_purged");

  return true;
}

bool gen_block_unlock_time_is_low::generate(std::vector<test_event_entry>& events) const
{
  BLOCK_VALIDATION_INIT_GENERATE();

  MAKE_MINER_TX_MANUALLY(miner_tx, blk_0);
  --miner_tx.unlockTime;

  Block blk_1;
  generator.constructBlockManually(blk_1, blk_0, miner_account, 
    test_generator::bf_major_ver | test_generator::bf_miner_tx, m_blockMajorVersion, 0, 0, crypto::hash(), 0, miner_tx);
  events.push_back(blk_1);

  DO_CALLBACK(events, "check_block_purged");

  return true;
}

bool gen_block_unlock_time_is_high::generate(std::vector<test_event_entry>& events) const
{
  BLOCK_VALIDATION_INIT_GENERATE();

  MAKE_MINER_TX_MANUALLY(miner_tx, blk_0);
  ++miner_tx.unlockTime;

  Block blk_1;
  generator.constructBlockManually(blk_1, blk_0, miner_account, 
    test_generator::bf_major_ver | test_generator::bf_miner_tx, m_blockMajorVersion, 0, 0, crypto::hash(), 0, miner_tx);
  events.push_back(blk_1);

  DO_CALLBACK(events, "check_block_purged");

  return true;
}

bool gen_block_unlock_time_is_timestamp_in_past::generate(std::vector<test_event_entry>& events) const
{
  BLOCK_VALIDATION_INIT_GENERATE();

  MAKE_MINER_TX_MANUALLY(miner_tx, blk_0);
  miner_tx.unlockTime = blk_0.timestamp - 10 * 60;

  Block blk_1;
  generator.constructBlockManually(blk_1, blk_0, miner_account, 
    test_generator::bf_major_ver | test_generator::bf_miner_tx, m_blockMajorVersion, 0, 0, crypto::hash(), 0, miner_tx);
  events.push_back(blk_1);

  DO_CALLBACK(events, "check_block_purged");

  return true;
}

bool gen_block_unlock_time_is_timestamp_in_future::generate(std::vector<test_event_entry>& events) const
{
  BLOCK_VALIDATION_INIT_GENERATE();

  MAKE_MINER_TX_MANUALLY(miner_tx, blk_0);
  miner_tx.unlockTime = blk_0.timestamp + 3 * m_currency.minedMoneyUnlockWindow() * m_currency.difficultyTarget();

  Block blk_1;
  generator.constructBlockManually(blk_1, blk_0, miner_account, 
    test_generator::bf_major_ver | test_generator::bf_miner_tx, m_blockMajorVersion, 0, 0, crypto::hash(), 0, miner_tx);
  events.push_back(blk_1);

  DO_CALLBACK(events, "check_block_purged");

  return true;
}

bool gen_block_height_is_low::generate(std::vector<test_event_entry>& events) const
{
  BLOCK_VALIDATION_INIT_GENERATE();

  MAKE_MINER_TX_MANUALLY(miner_tx, blk_0);
  boost::get<TransactionInputGenerate>(miner_tx.vin[0]).height--;

  Block blk_1;
  generator.constructBlockManually(blk_1, blk_0, miner_account, 
    test_generator::bf_major_ver | test_generator::bf_miner_tx, m_blockMajorVersion, 0, 0, crypto::hash(), 0, miner_tx);
  events.push_back(blk_1);

  DO_CALLBACK(events, "check_block_purged");

  return true;
}

bool gen_block_height_is_high::generate(std::vector<test_event_entry>& events) const
{
  BLOCK_VALIDATION_INIT_GENERATE();

  MAKE_MINER_TX_MANUALLY(miner_tx, blk_0);
  boost::get<TransactionInputGenerate>(miner_tx.vin[0]).height++;

  Block blk_1;
  generator.constructBlockManually(blk_1, blk_0, miner_account, 
    test_generator::bf_major_ver | test_generator::bf_miner_tx, m_blockMajorVersion, 0, 0, crypto::hash(), 0, miner_tx);
  events.push_back(blk_1);

  DO_CALLBACK(events, "check_block_purged");

  return true;
}

bool gen_block_miner_tx_has_2_tx_gen_in::generate(std::vector<test_event_entry>& events) const
{
  BLOCK_VALIDATION_INIT_GENERATE();

  MAKE_MINER_TX_MANUALLY(miner_tx, blk_0);

  TransactionInputGenerate in;
  in.height = get_block_height(blk_0) + 1;
  miner_tx.vin.push_back(in);

  Block blk_1;
  generator.constructBlockManually(blk_1, blk_0, miner_account, 
    test_generator::bf_major_ver | test_generator::bf_miner_tx, m_blockMajorVersion, 0, 0, crypto::hash(), 0, miner_tx);
  events.push_back(blk_1);

  DO_CALLBACK(events, "check_block_purged");

  return true;
}

bool gen_block_miner_tx_has_2_in::generate(std::vector<test_event_entry>& events) const
{
  BLOCK_VALIDATION_INIT_GENERATE();

  generator.defaultMajorVersion = m_blockMajorVersion;

  REWIND_BLOCKS(events, blk_0r, blk_0, miner_account);

  GENERATE_ACCOUNT(alice);

  tx_source_entry se;
  se.amount = blk_0.minerTx.vout[0].amount;
  se.outputs.push_back(std::make_pair(0, boost::get<TransactionOutputToKey>(blk_0.minerTx.vout[0].target).key));
  se.real_output = 0;
  se.real_out_tx_key = get_tx_pub_key_from_extra(blk_0.minerTx);
  se.real_output_in_tx_index = 0;
  std::vector<tx_source_entry> sources;
  sources.push_back(se);

  tx_destination_entry de;
  de.addr = miner_account.get_keys().m_account_address;
  de.amount = se.amount;
  std::vector<tx_destination_entry> destinations;
  destinations.push_back(de);

  Transaction tmp_tx;
  if (!construct_tx(miner_account.get_keys(), sources, destinations, std::vector<uint8_t>(), tmp_tx, 0))
    return false;

  MAKE_MINER_TX_MANUALLY(miner_tx, blk_0);
  miner_tx.vin.push_back(tmp_tx.vin[0]);

  Block blk_1;
  generator.constructBlockManually(blk_1, blk_0r, miner_account, 
    test_generator::bf_major_ver | test_generator::bf_miner_tx, m_blockMajorVersion, 0, 0, crypto::hash(), 0, miner_tx);
  events.push_back(blk_1);

  DO_CALLBACK(events, "check_block_purged");

  return true;
}

bool gen_block_miner_tx_with_txin_to_key::generate(std::vector<test_event_entry>& events) const
{
  BLOCK_VALIDATION_INIT_GENERATE();

  generator.defaultMajorVersion = m_blockMajorVersion;

  // This block has only one output
  Block blk_1;
  generator.constructBlockManually(blk_1, blk_0, miner_account, test_generator::bf_none);
  events.push_back(blk_1);

  REWIND_BLOCKS(events, blk_1r, blk_1, miner_account);

  tx_source_entry se;
  se.amount = blk_1.minerTx.vout[0].amount;
  se.outputs.push_back(std::make_pair(0, boost::get<TransactionOutputToKey>(blk_1.minerTx.vout[0].target).key));
  se.real_output = 0;
  se.real_out_tx_key = get_tx_pub_key_from_extra(blk_1.minerTx);
  se.real_output_in_tx_index = 0;
  std::vector<tx_source_entry> sources;
  sources.push_back(se);

  tx_destination_entry de;
  de.addr = miner_account.get_keys().m_account_address;
  de.amount = se.amount;
  std::vector<tx_destination_entry> destinations;
  destinations.push_back(de);

  Transaction tmp_tx;
  if (!construct_tx(miner_account.get_keys(), sources, destinations, std::vector<uint8_t>(), tmp_tx, 0))
    return false;

  MAKE_MINER_TX_MANUALLY(miner_tx, blk_1);
  miner_tx.vin[0] = tmp_tx.vin[0];

  Block blk_2;
  generator.constructBlockManually(blk_2, blk_1r, miner_account, 
    test_generator::bf_major_ver | test_generator::bf_miner_tx, m_blockMajorVersion, 0, 0, crypto::hash(), 0, miner_tx);
  events.push_back(blk_2);

  DO_CALLBACK(events, "check_block_purged");

  return true;
}

bool gen_block_miner_tx_out_is_small::generate(std::vector<test_event_entry>& events) const
{
  BLOCK_VALIDATION_INIT_GENERATE();

  MAKE_MINER_TX_MANUALLY(miner_tx, blk_0);
  miner_tx.vout[0].amount /= 2;

  Block blk_1;
  generator.constructBlockManually(blk_1, blk_0, miner_account, 
    test_generator::bf_major_ver | test_generator::bf_miner_tx, m_blockMajorVersion, 0, 0, crypto::hash(), 0, miner_tx);
  events.push_back(blk_1);

  DO_CALLBACK(events, "check_block_purged");

  return true;
}

bool gen_block_miner_tx_out_is_big::generate(std::vector<test_event_entry>& events) const
{
  BLOCK_VALIDATION_INIT_GENERATE();

  MAKE_MINER_TX_MANUALLY(miner_tx, blk_0);
  miner_tx.vout[0].amount *= 2;

  Block blk_1;
  generator.constructBlockManually(blk_1, blk_0, miner_account, 
    test_generator::bf_major_ver | test_generator::bf_miner_tx, m_blockMajorVersion, 0, 0, crypto::hash(), 0, miner_tx);
  events.push_back(blk_1);

  DO_CALLBACK(events, "check_block_purged");

  return true;
}

bool gen_block_miner_tx_has_no_out::generate(std::vector<test_event_entry>& events) const
{
  BLOCK_VALIDATION_INIT_GENERATE();

  MAKE_MINER_TX_MANUALLY(miner_tx, blk_0);
  miner_tx.vout.clear();

  Block blk_1;
  generator.constructBlockManually(blk_1, blk_0, miner_account, 
    test_generator::bf_major_ver | test_generator::bf_miner_tx, m_blockMajorVersion, 0, 0, crypto::hash(), 0, miner_tx);
  events.push_back(blk_1);

  DO_CALLBACK(events, "check_block_purged");

  return true;
}

bool gen_block_miner_tx_has_out_to_alice::generate(std::vector<test_event_entry>& events) const
{
  BLOCK_VALIDATION_INIT_GENERATE();

  GENERATE_ACCOUNT(alice);

  KeyPair txkey;
  MAKE_MINER_TX_AND_KEY_MANUALLY(miner_tx, blk_0, &txkey);

  crypto::key_derivation derivation;
  crypto::public_key out_eph_public_key;
  crypto::generate_key_derivation(alice.get_keys().m_account_address.m_viewPublicKey, txkey.sec, derivation);
  crypto::derive_public_key(derivation, 1, alice.get_keys().m_account_address.m_spendPublicKey, out_eph_public_key);

  TransactionOutput out_to_alice;
  out_to_alice.amount = miner_tx.vout[0].amount / 2;
  miner_tx.vout[0].amount -= out_to_alice.amount;
  out_to_alice.target = TransactionOutputToKey(out_eph_public_key);
  miner_tx.vout.push_back(out_to_alice);

  Block blk_1;
  generator.constructBlockManually(blk_1, blk_0, miner_account, 
    test_generator::bf_major_ver | test_generator::bf_miner_tx, m_blockMajorVersion, 0, 0, crypto::hash(), 0, miner_tx);
  events.push_back(blk_1);

  DO_CALLBACK(events, "check_block_accepted");

  return true;
}

bool gen_block_has_invalid_tx::generate(std::vector<test_event_entry>& events) const
{
  BLOCK_VALIDATION_INIT_GENERATE();

  generator.defaultMajorVersion = m_blockMajorVersion;

  std::vector<crypto::hash> tx_hashes;
  tx_hashes.push_back(crypto::hash());

  Block blk_1;
  generator.constructBlockManuallyTx(blk_1, blk_0, miner_account, tx_hashes, 0);
  events.push_back(blk_1);

  DO_CALLBACK(events, "check_block_purged");

  return true;
}

bool gen_block_is_too_big::generate(std::vector<test_event_entry>& events) const
{
  BLOCK_VALIDATION_INIT_GENERATE();
  generator.defaultMajorVersion = m_blockMajorVersion;

  Block blk_1;
  if (!generator.constructMaxSizeBlock(blk_1, blk_0, miner_account)) {
    return false;
  }

  blk_1.minerTx.extra.resize(blk_1.minerTx.extra.size() + 1);
  events.push_back(blk_1);

  DO_CALLBACK(events, "check_block_purged");

  return true;
}

bool TestBlockCumulativeSizeExceedsLimit::generate(std::vector<test_event_entry>& events) const {
  BLOCK_VALIDATION_INIT_GENERATE();

  generator.defaultMajorVersion = m_blockMajorVersion;

  Block prevBlock = blk_0;
  for (size_t height = 1; height < 1000; ++height) {
    Block block;
    if (!generator.constructMaxSizeBlock(block, prevBlock, miner_account)) {
      return false;
    }

    prevBlock = block;

    if (get_object_blobsize(block.minerTx) <= m_currency.maxBlockCumulativeSize(height)) {
      events.push_back(block);
    } else {
      DO_CALLBACK(events, "markInvalidBlock");
      events.push_back(block);
      return true;
    }
  }

  return false;
}

gen_block_invalid_binary_format::gen_block_invalid_binary_format(uint8_t blockMajorVersion) : 
    m_corrupt_blocks_begin_idx(0),
    m_blockMajorVersion(blockMajorVersion) {
  cryptonote::CurrencyBuilder currencyBuilder;
  currencyBuilder.upgradeHeight(blockMajorVersion == cryptonote::BLOCK_MAJOR_VERSION_1 ? UNDEF_HEIGHT : 0);
  m_currency = currencyBuilder.currency();

  REGISTER_CALLBACK("check_all_blocks_purged", gen_block_invalid_binary_format::check_all_blocks_purged);
  REGISTER_CALLBACK("corrupt_blocks_boundary", gen_block_invalid_binary_format::corrupt_blocks_boundary);
}

bool gen_block_invalid_binary_format::generate(std::vector<test_event_entry>& events) const
{
  BLOCK_VALIDATION_INIT_GENERATE();

  generator.defaultMajorVersion = m_blockMajorVersion;

  std::vector<uint64_t> timestamps;
  std::vector<difficulty_type> cummulative_difficulties;
  difficulty_type cummulative_diff = 1;

  // Unlock blk_0 outputs
  Block blk_last = blk_0;
  assert(m_currency.minedMoneyUnlockWindow() < m_currency.difficultyWindow());
  for (size_t i = 0; i < m_currency.minedMoneyUnlockWindow(); ++i)
  {
    MAKE_NEXT_BLOCK(events, blk_curr, blk_last, miner_account);
    timestamps.push_back(blk_curr.timestamp);
    cummulative_difficulties.push_back(++cummulative_diff);
    blk_last = blk_curr;
  }

  // Lifting up takes a while
  difficulty_type diffic;
  do
  {
    blk_last = boost::get<Block>(events.back());
    diffic = m_currency.nextDifficulty(timestamps, cummulative_difficulties);
    if (!lift_up_difficulty(m_currency, events, timestamps, cummulative_difficulties, generator, 1, blk_last,
      miner_account, m_blockMajorVersion)) {
      return false;
    }
    std::cout << "Block #" << events.size() << ", difficulty: " << diffic << std::endl;
  }
  while (diffic < 1500);

  blk_last = boost::get<Block>(events.back());
  MAKE_TX(events, tx_0, miner_account, miner_account, MK_COINS(120), boost::get<Block>(events[1]));
  DO_CALLBACK(events, "corrupt_blocks_boundary");

  Block blk_test;
  std::vector<crypto::hash> tx_hashes;
  tx_hashes.push_back(get_transaction_hash(tx_0));
  size_t txs_size = get_object_blobsize(tx_0);
  diffic = m_currency.nextDifficulty(timestamps, cummulative_difficulties);
  if (!generator.constructBlockManually(blk_test, blk_last, miner_account,
    test_generator::bf_major_ver | test_generator::bf_diffic | test_generator::bf_timestamp | test_generator::bf_tx_hashes, 
    m_blockMajorVersion, 0, blk_last.timestamp, crypto::hash(), diffic, Transaction(), tx_hashes, txs_size))
    return false;

  blobdata blob = t_serializable_object_to_blob(blk_test);
  for (size_t i = 0; i < blob.size(); ++i)
  {
    for (size_t bit_idx = 0; bit_idx < sizeof(blobdata::value_type) * 8; ++bit_idx)
    {
      serialized_block sr_block(blob);
      blobdata::value_type& ch = sr_block.data[i];
      ch ^= 1 << bit_idx;

      events.push_back(sr_block);
    }
  }

  DO_CALLBACK(events, "check_all_blocks_purged");

  return true;
}

bool gen_block_invalid_binary_format::check_block_verification_context(const cryptonote::block_verification_context& bvc,
                                                                       size_t event_idx, const cryptonote::Block& blk)
{
  if (0 == m_corrupt_blocks_begin_idx || event_idx < m_corrupt_blocks_begin_idx)
  {
    return bvc.m_added_to_main_chain;
  }
  else
  {
    return !bvc.m_added_to_main_chain && (bvc.m_already_exists || bvc.m_marked_as_orphaned || bvc.m_verifivation_failed);
  }
}

bool gen_block_invalid_binary_format::corrupt_blocks_boundary(cryptonote::core& c, size_t ev_index, const std::vector<test_event_entry>& events)
{
  m_corrupt_blocks_begin_idx = ev_index + 1;
  return true;
}

bool gen_block_invalid_binary_format::check_all_blocks_purged(cryptonote::core& c, size_t ev_index, const std::vector<test_event_entry>& events)
{
  DEFINE_TESTS_ERROR_CONTEXT("gen_block_invalid_binary_format::check_all_blocks_purged");

  CHECK_EQ(1, c.get_pool_transactions_count());
  CHECK_EQ(m_corrupt_blocks_begin_idx - 2, c.get_current_blockchain_height());

  return true;
}

bool TestMaxSizeOfParentBlock::generate(std::vector<test_event_entry>& events) const {
  BLOCK_VALIDATION_INIT_GENERATE();

  cryptonote::Block blk_1;
  generator.constructBlockManually(blk_1, blk_0, miner_account, test_generator::bf_major_ver, BLOCK_MAJOR_VERSION_2);
  if (!adjustParentBlockSize(blk_1, 2 * 1024)) {
    return false;
  }
  events.push_back(blk_1);

  DO_CALLBACK(events, "check_block_accepted");

  return true;
}

bool TestBigParentBlock::generate(std::vector<test_event_entry>& events) const {
  BLOCK_VALIDATION_INIT_GENERATE();

  cryptonote::Block blk_1;
  generator.constructBlockManually(blk_1, blk_0, miner_account, test_generator::bf_major_ver, BLOCK_MAJOR_VERSION_2);
  if (!adjustParentBlockSize(blk_1, 2 * 1024 + 1)) {
    return false;
  }
  events.push_back(blk_1);

  DO_CALLBACK(events, "check_block_purged");

  return true;
}


namespace
{
  template <typename MutateFunc>
  bool GenerateAndMutateBlockV2(const cryptonote::Currency& currency, std::vector<test_event_entry>& events, const std::string& callback, MutateFunc mf) {
    TestGenerator bg(currency, events);

    cryptonote::Block blk_1;
    bg.generator.constructBlockManually(
      blk_1, bg.lastBlock, bg.minerAccount, test_generator::bf_major_ver, BLOCK_MAJOR_VERSION_2);

    mf(blk_1);

    events.push_back(blk_1);
    bg.addCallback(callback);

    return true;
  }
}

bool TestBlock2ExtraEmpty::generate(std::vector<test_event_entry>& events) const {
  return GenerateAndMutateBlockV2(m_currency, events, "check_block_purged", [](cryptonote::Block& blk) {
    blk.parentBlock.minerTx.extra.clear();
  });
}

bool TestBlock2ExtraWithoutMMTag::generate(std::vector<test_event_entry>& events) const {
  return GenerateAndMutateBlockV2(m_currency, events, "check_block_purged", [](cryptonote::Block& blk) {
    blk.parentBlock.minerTx.extra.clear();
    cryptonote::add_extra_nonce_to_tx_extra(blk.parentBlock.minerTx.extra, "0xdeadbeef");
  });
}

bool TestBlock2ExtraWithGarbage::generate(std::vector<test_event_entry>& events) const {
  return GenerateAndMutateBlockV2(m_currency, events, "check_block_accepted", [](cryptonote::Block& blk) {
    cryptonote::add_extra_nonce_to_tx_extra(blk.parentBlock.minerTx.extra, "0xdeadbeef");
    blk.parentBlock.minerTx.extra.push_back(0xde);
    blk.parentBlock.minerTx.extra.push_back(0xad);
    blk.parentBlock.minerTx.extra.push_back(0xbe);
    blk.parentBlock.minerTx.extra.push_back(0xef);
  });
}
