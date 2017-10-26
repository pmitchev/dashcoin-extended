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

// The copyright below is only for 'transfer' related declaration
// Copyright (c) 2014-2015, The Monero Project
// 
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
// 
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
// 
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
// 
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


#pragma once

#include <memory>
#include <boost/serialization/list.hpp>
#include <boost/serialization/vector.hpp>
#include <atomic>

#include "include_base_utils.h"
#include "cryptonote_core/account_boost_serialization.h"
#include "cryptonote_core/cryptonote_basic_impl.h"
#include "cryptonote_core/Currency.h"
#include "net/http_client.h"
#include "storages/http_abstract_invoke.h"
#include "rpc/core_rpc_server_commands_defs.h"
#include "cryptonote_core/cryptonote_format_utils.h"
#include "common/unordered_containers_boost_serialization.h"
#include "crypto/chacha8.h"
#include "crypto/hash.h"
#include "common/BlockingQueue.h"

#include "wallet_errors.h"

#define DEFAULT_TX_SPENDABLE_AGE                               10
#define WALLET_RCP_CONNECTION_TIMEOUT                          200000

namespace tools
{
  class i_wallet2_callback
  {
  public:
    virtual void on_new_block(uint64_t height) {}
    virtual void on_money_received(uint64_t height, const cryptonote::Transaction& tx, size_t out_index) {}
    virtual void on_money_spent(uint64_t height, const cryptonote::Transaction& in_tx, size_t out_index, const cryptonote::Transaction& spend_tx) {}
    virtual void on_skip_transaction(uint64_t height, const cryptonote::Transaction& tx) {}
  };

  struct tx_dust_policy
  {
    uint64_t dust_threshold;
    bool add_to_fee;
    cryptonote::AccountPublicAddress addr_for_dust;

    tx_dust_policy(uint64_t a_dust_threshold = 0, bool an_add_to_fee = true,
                   cryptonote::AccountPublicAddress an_addr_for_dust = cryptonote::AccountPublicAddress())
      : dust_threshold(a_dust_threshold)
      , add_to_fee(an_add_to_fee)
      , addr_for_dust(an_addr_for_dust)
    {
    }
  };

  class wallet2 {
    wallet2(const wallet2& rhs) :
      m_currency(rhs.m_currency),
      m_run(true),
      m_callback(0) {
    };

  public:
    wallet2(const cryptonote::Currency& currency) :
      m_currency(currency), m_run(true), m_callback(0) {
    }

    struct transfer_details
    {
      uint64_t m_block_height;
      cryptonote::Transaction m_tx;
      size_t m_internal_output_index;
      uint64_t m_global_output_index;
      bool m_spent;
      crypto::key_image m_key_image; //TODO: key_image stored twice :(

      uint64_t amount() const { return m_tx.vout[m_internal_output_index].amount; }
    };

    struct payment_details
    {
      crypto::hash m_tx_hash;
      uint64_t m_amount;
      uint64_t m_block_height;
      uint64_t m_unlock_time;
    };

    struct unconfirmed_transfer_details
    {
      cryptonote::Transaction m_tx;
      uint64_t m_change;
      time_t m_sent_time;
    };

    typedef std::vector<transfer_details> transfer_container;
    typedef std::unordered_multimap<crypto::hash, payment_details> payment_container;

    struct pending_tx
    {
      cryptonote::Transaction tx;
      uint64_t dust, fee;
      cryptonote::tx_destination_entry change_dts;
      std::list<transfer_container::iterator> selected_transfers;
      std::string key_images;
    };

    struct keys_file_data
    {
      crypto::chacha8_iv iv;
      std::string account_data;

      BEGIN_SERIALIZE_OBJECT()
        FIELD(iv)
        FIELD(account_data)
      END_SERIALIZE()
    };

    struct Transfer {
      uint64_t time;
      bool output;
      crypto::hash transactionHash;
      uint64_t amount;
      uint64_t fee;
      crypto::hash paymentId;
      bool hasAddress;
      cryptonote::AccountPublicAddress address;
      uint64_t blockIndex;
      uint64_t unlockTime;

      template <class Archive> void serialize(Archive& archive, unsigned int version) {
        archive & time;
        archive & output;
        archive & transactionHash;
        archive & amount;
        archive & fee;
        archive & paymentId;
        archive & hasAddress;
        if (hasAddress) {
          archive & address;
        }

        archive & blockIndex;
        archive & unlockTime;
      }
    };

    typedef std::vector<Transfer> Transfers;

    void generate(const std::string& wallet, const std::string& password);
    void load(const std::string& wallet, const std::string& password);
    void store();
    cryptonote::account_base& get_account(){return m_account;}

    void init(const std::string& daemon_address = "http://localhost:8080");
    bool deinit();

    void stop() { m_run.store(false, std::memory_order_relaxed); }

    const cryptonote::Currency currency() const { return m_currency; }

    i_wallet2_callback* callback() const { return m_callback; }
    void callback(i_wallet2_callback* callback) { m_callback = callback; }

    void refresh();
    void refresh(size_t & blocks_fetched);
    void refresh(size_t & blocks_fetched, bool& received_money);
    bool refresh(size_t & blocks_fetched, bool& received_money, bool& ok);

    uint64_t balance();
    uint64_t unlocked_balance();
    template<typename T>
    void transfer(const std::vector<cryptonote::tx_destination_entry>& dsts, size_t fake_outputs_count, uint64_t unlock_time, uint64_t fee, const std::vector<uint8_t>& extra, T destination_split_strategy, const tx_dust_policy& dust_policy);
    template<typename T>
    void transfer(const std::vector<cryptonote::tx_destination_entry>& dsts, size_t fake_outputs_count, uint64_t unlock_time, uint64_t fee, const std::vector<uint8_t>& extra, T destination_split_strategy, const tx_dust_policy& dust_policy, cryptonote::Transaction& tx, pending_tx& ptx);
    void transfer(const std::vector<cryptonote::tx_destination_entry>& dsts, size_t fake_outputs_count, uint64_t unlock_time, uint64_t fee, const std::vector<uint8_t>& extra);
    void transfer(const std::vector<cryptonote::tx_destination_entry>& dsts, size_t fake_outputs_count, uint64_t unlock_time, uint64_t fee, const std::vector<uint8_t>& extra, cryptonote::Transaction& tx, pending_tx& ptx);
    void commit_tx(pending_tx& ptx_vector);
    void commit_tx(std::vector<pending_tx>& ptx_vector);
    std::vector<pending_tx> create_transactions(std::vector<cryptonote::tx_destination_entry> dsts, const size_t fake_outs_count, const uint64_t unlock_time, const uint64_t fee, const std::vector<uint8_t> extra);
    bool check_connection();
    bool connectClient(epee::net_utils::http::http_simple_client& client);
    void get_transfers(wallet2::transfer_container& incoming_transfers) const;
    void get_payments(const crypto::hash& payment_id, std::list<wallet2::payment_details>& payments, uint64_t min_height = 0) const;
    void get_payments(std::list<std::pair<crypto::hash,wallet2::payment_details>>& payments, uint64_t min_height) const;
    uint64_t get_blockchain_current_height() const { return m_blockchain.size(); }
    const std::vector<Transfer>& getTransfers();
    void reset();

    template <class t_archive>
    inline void serialize(t_archive &a, const unsigned int ver)
    {
      if(ver < 5)
        return;
      a & m_blockchain;
      a & m_transfers;
      a & m_account_public_address;
      a & m_key_images;
      if(ver < 6)
        return;
      a & m_unconfirmed_txs;
      if(ver < 7)
        return;
      a & m_payments;
      if (ver >= 8) {
        a & transfers;
      }
    }

    static void wallet_exists(const std::string& file_path, bool& keys_file_exists, bool& wallet_file_exists);
    static bool parse_payment_id(const std::string& payment_id_str, crypto::hash& payment_id);

  private:
    bool store_keys(const std::string& keys_file_name, const std::string& password);
    void load_keys(const std::string& keys_file_name, const std::string& password);
    
    struct TxItem {
      cryptonote::Transaction tx;
      uint64_t time;
      uint64_t height;
      crypto::hash blockId;
      crypto::public_key txPubKey;
      std::vector<cryptonote::tx_extra_field> txExtraFields;

      // resolved
      std::vector<size_t> outs;
      uint64_t txMoneyGotInOuts;
    };

    typedef std::unique_ptr<TxItem> TxQueueItem;
    typedef BlockingQueue<TxQueueItem> TxQueue;

    bool addNewBlockchainEntry(const crypto::hash& bl_id, uint64_t start_height, uint64_t height);
    
    size_t processNewBlockchainEntry(TxQueue& queue, const cryptonote::block_complete_entry& bche, const crypto::hash& bl_id, uint64_t height);
    bool processNewTransaction(TxQueue& queue, const cryptonote::Transaction& tx, uint64_t height, uint64_t time, const crypto::hash& bl_id);
    void processCheckedTransaction(const TxItem& item);

    // returns number of blocks added
    size_t updateBlockchain(const cryptonote::COMMAND_RPC_QUERY_BLOCKS::response& res, std::unordered_set<crypto::hash>& newBlocks);
    void processTransactions(const cryptonote::COMMAND_RPC_QUERY_BLOCKS::response& res, const std::unordered_set<crypto::hash>& newBlocks);
    cryptonote::COMMAND_RPC_QUERY_BLOCKS::response queryBlocks(epee::net_utils::http::http_simple_client& client);

    size_t detach_blockchain(uint64_t height);
    void get_short_chain_history(std::list<crypto::hash>& ids) const;
    bool is_tx_spendtime_unlocked(uint64_t unlock_time) const;
    bool is_transfer_unlocked(const transfer_details& td) const;
    bool clear();
    uint64_t select_transfers(uint64_t needed_money, bool add_dust, uint64_t dust, std::list<transfer_container::iterator>& selected_transfers);
    bool prepare_file_names(const std::string& file_path);
    void process_unconfirmed(const cryptonote::Transaction& tx);
    void add_unconfirmed_tx(const cryptonote::Transaction& tx, uint64_t change_amount);
    void checkGenesis(const crypto::hash& genesis_hash); //throws

    inline void print_source_entry(const cryptonote::tx_source_entry& src) {
      std::string indexes;
      std::for_each(src.outputs.begin(), src.outputs.end(), [&](const cryptonote::tx_source_entry::output_entry& s_e) {
          indexes += std::to_string(s_e.first) + " ";
      });
      LOG_PRINT_L0("amount=" << m_currency.formatAmount(src.amount) <<
        ", real_output=" << src.real_output <<
        ", real_output_in_tx_index=" << src.real_output_in_tx_index <<
        ", indexes: " << indexes);
    }

  private:
    const cryptonote::Currency& m_currency;
    cryptonote::account_base m_account;
    std::string m_daemon_address;
    std::string m_wallet_file;
    std::string m_keys_file;
    epee::net_utils::http::http_simple_client m_http_client;
    std::vector<crypto::hash> m_blockchain;
    std::unordered_map<crypto::hash, unconfirmed_transfer_details> m_unconfirmed_txs;

    transfer_container m_transfers;
    payment_container m_payments;
    std::unordered_map<crypto::key_image, size_t> m_key_images;
    cryptonote::AccountPublicAddress m_account_public_address;

    std::atomic<bool> m_run;

    i_wallet2_callback* m_callback;

    Transfers transfers;
  };
}
BOOST_CLASS_VERSION(tools::wallet2, 8)

namespace boost
{
  namespace serialization
  {
    template <class Archive>
    inline void serialize(Archive &a, tools::wallet2::transfer_details &x, const boost::serialization::version_type ver)
    {
      a & x.m_block_height;
      a & x.m_global_output_index;
      a & x.m_internal_output_index;
      a & x.m_tx;
      a & x.m_spent;
      a & x.m_key_image;
    }

    template <class Archive>
    inline void serialize(Archive &a, tools::wallet2::unconfirmed_transfer_details &x, const boost::serialization::version_type ver)
    {
      a & x.m_change;
      a & x.m_sent_time;
      a & x.m_tx;
    }

    template <class Archive>
    inline void serialize(Archive& a, tools::wallet2::payment_details& x, const boost::serialization::version_type ver)
    {
      a & x.m_tx_hash;
      a & x.m_amount;
      a & x.m_block_height;
      a & x.m_unlock_time;
    }
  }
}

namespace tools
{
  namespace detail
  {
    //----------------------------------------------------------------------------------------------------
    inline void digit_split_strategy(const std::vector<cryptonote::tx_destination_entry>& dsts,
      const cryptonote::tx_destination_entry& change_dst, uint64_t dust_threshold,
      std::vector<cryptonote::tx_destination_entry>& splitted_dsts, uint64_t& dust)
    {
      splitted_dsts.clear();
      dust = 0;

      BOOST_FOREACH(auto& de, dsts)
      {
        cryptonote::decompose_amount_into_digits(de.amount, dust_threshold,
          [&](uint64_t chunk) { splitted_dsts.push_back(cryptonote::tx_destination_entry(chunk, de.addr)); },
          [&](uint64_t a_dust) { splitted_dsts.push_back(cryptonote::tx_destination_entry(a_dust, de.addr)); } );
      }

      cryptonote::decompose_amount_into_digits(change_dst.amount, dust_threshold,
        [&](uint64_t chunk) { splitted_dsts.push_back(cryptonote::tx_destination_entry(chunk, change_dst.addr)); },
        [&](uint64_t a_dust) { dust = a_dust; } );
    }
    //----------------------------------------------------------------------------------------------------
    inline void null_split_strategy(const std::vector<cryptonote::tx_destination_entry>& dsts,
      const cryptonote::tx_destination_entry& change_dst, uint64_t dust_threshold,
      std::vector<cryptonote::tx_destination_entry>& splitted_dsts, uint64_t& dust)
    {
      splitted_dsts = dsts;

      dust = 0;
      uint64_t change = change_dst.amount;
      if (0 < dust_threshold)
      {
        for (uint64_t order = 10; order <= 10 * dust_threshold; order *= 10)
        {
          uint64_t dust_candidate = change_dst.amount % order;
          uint64_t change_candidate = (change_dst.amount / order) * order;
          if (dust_candidate <= dust_threshold)
          {
            dust = dust_candidate;
            change = change_candidate;
          }
          else
          {
            break;
          }
        }
      }

      if (0 != change)
      {
        splitted_dsts.push_back(cryptonote::tx_destination_entry(change, change_dst.addr));
      }
    }
    //----------------------------------------------------------------------------------------------------
  }
  //----------------------------------------------------------------------------------------------------
  template<typename T>
  void wallet2::transfer(const std::vector<cryptonote::tx_destination_entry>& dsts, size_t fake_outputs_count,
    uint64_t unlock_time, uint64_t fee, const std::vector<uint8_t>& extra, T destination_split_strategy, const tx_dust_policy& dust_policy)
  {
    cryptonote::Transaction tx;
    transfer(dsts, fake_outputs_count, unlock_time, fee, extra, destination_split_strategy, dust_policy, tx);
  }

  template<typename T>
  void wallet2::transfer(const std::vector<cryptonote::tx_destination_entry>& dsts, size_t fake_outputs_count,
    uint64_t unlock_time, uint64_t fee, const std::vector<uint8_t>& extra, T destination_split_strategy, const tx_dust_policy& dust_policy, cryptonote::Transaction &tx, pending_tx &ptx)
  {
    using namespace cryptonote;
    THROW_WALLET_EXCEPTION_IF(dsts.empty(), error::zero_destination);

    uint64_t needed_money = fee;
    BOOST_FOREACH(auto& dt, dsts)
    {
      THROW_WALLET_EXCEPTION_IF(0 == dt.amount, error::zero_destination);
      needed_money += dt.amount;
      THROW_WALLET_EXCEPTION_IF(needed_money < dt.amount, error::tx_sum_overflow, m_currency.publicAddressBase58Prefix(), dsts, fee);
    }

    std::list<transfer_container::iterator> selected_transfers;
    uint64_t found_money = select_transfers(needed_money, 0 == fake_outputs_count, dust_policy.dust_threshold, selected_transfers);
    THROW_WALLET_EXCEPTION_IF(found_money < needed_money, error::not_enough_money, found_money, needed_money - fee, fee);

    typedef COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::out_entry out_entry;
    typedef cryptonote::tx_source_entry::output_entry tx_output_entry;

    COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::response daemon_resp = AUTO_VAL_INIT(daemon_resp);
    if(fake_outputs_count)
    {
      COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::request req = AUTO_VAL_INIT(req);
      req.outs_count = fake_outputs_count + 1;// add one to make possible (if need) to skip real output key
      BOOST_FOREACH(transfer_container::iterator it, selected_transfers)
      {
        THROW_WALLET_EXCEPTION_IF(it->m_tx.vout.size() <= it->m_internal_output_index, error::wallet_internal_error,
          "m_internal_output_index = " + std::to_string(it->m_internal_output_index) +
          " is greater or equal to outputs count = " + std::to_string(it->m_tx.vout.size()));
        req.amounts.push_back(it->amount());
      }

      bool r = epee::net_utils::invoke_http_bin_remote_command2(m_daemon_address + "/getrandom_outs.bin", req, daemon_resp, m_http_client, 200000);
      THROW_WALLET_EXCEPTION_IF(!r, error::no_connection_to_daemon, "getrandom_outs.bin");
      THROW_WALLET_EXCEPTION_IF(daemon_resp.status == CORE_RPC_STATUS_BUSY, error::daemon_busy, "getrandom_outs.bin");
      THROW_WALLET_EXCEPTION_IF(daemon_resp.status != CORE_RPC_STATUS_OK, error::get_random_outs_error, daemon_resp.status);
      THROW_WALLET_EXCEPTION_IF(daemon_resp.outs.size() != selected_transfers.size(), error::wallet_internal_error,
        "daemon returned wrong response for getrandom_outs.bin, wrong amounts count = " +
        std::to_string(daemon_resp.outs.size()) + ", expected " +  std::to_string(selected_transfers.size()));

      std::vector<COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::outs_for_amount> scanty_outs;
      BOOST_FOREACH(COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::outs_for_amount& amount_outs, daemon_resp.outs)
      {
        if (amount_outs.outs.size() < fake_outputs_count)
        {
          scanty_outs.push_back(amount_outs);
        }
      }
      THROW_WALLET_EXCEPTION_IF(!scanty_outs.empty(), error::not_enough_outs_to_mix, scanty_outs, fake_outputs_count);
    }

    //prepare inputs
    size_t i = 0;
    std::vector<cryptonote::tx_source_entry> sources;
    BOOST_FOREACH(transfer_container::iterator it, selected_transfers)
    {
      sources.resize(sources.size()+1);
      cryptonote::tx_source_entry& src = sources.back();
      transfer_details& td = *it;
      src.amount = td.amount();
      //paste mixin transaction
      if(daemon_resp.outs.size())
      {
        daemon_resp.outs[i].outs.sort([](const out_entry& a, const out_entry& b){return a.global_amount_index < b.global_amount_index;});
        BOOST_FOREACH(out_entry& daemon_oe, daemon_resp.outs[i].outs)
        {
          if(td.m_global_output_index == daemon_oe.global_amount_index)
            continue;
          tx_output_entry oe;
          oe.first = daemon_oe.global_amount_index;
          oe.second = daemon_oe.out_key;
          src.outputs.push_back(oe);
          if(src.outputs.size() >= fake_outputs_count)
            break;
        }
      }

      //paste real transaction to the random index
      auto it_to_insert = std::find_if(src.outputs.begin(), src.outputs.end(), [&](const tx_output_entry& a)
      {
        return a.first >= td.m_global_output_index;
      });
      //size_t real_index = src.outputs.size() ? (rand() % src.outputs.size() ):0;
      tx_output_entry real_oe;
      real_oe.first = td.m_global_output_index;
      real_oe.second = boost::get<TransactionOutputToKey>(td.m_tx.vout[td.m_internal_output_index].target).key;
      auto interted_it = src.outputs.insert(it_to_insert, real_oe);
      src.real_out_tx_key = get_tx_pub_key_from_extra(td.m_tx);
      src.real_output = interted_it - src.outputs.begin();
      src.real_output_in_tx_index = td.m_internal_output_index;
      print_source_entry(src);
      ++i;
    }

    cryptonote::tx_destination_entry change_dts = AUTO_VAL_INIT(change_dts);
    if (needed_money < found_money)
    {
      change_dts.addr = m_account.get_keys().m_account_address;
      change_dts.amount = found_money - needed_money;
    }

    uint64_t dust = 0;
    std::vector<cryptonote::tx_destination_entry> splitted_dsts;
    destination_split_strategy(dsts, change_dts, dust_policy.dust_threshold, splitted_dsts, dust);
    THROW_WALLET_EXCEPTION_IF(dust_policy.dust_threshold < dust, error::wallet_internal_error, "invalid dust value: dust = " +
      std::to_string(dust) + ", dust_threshold = " + std::to_string(dust_policy.dust_threshold));
    if (0 != dust && !dust_policy.add_to_fee)
    {
      splitted_dsts.push_back(cryptonote::tx_destination_entry(dust, dust_policy.addr_for_dust));
    }

    bool r = cryptonote::construct_tx(m_account.get_keys(), sources, splitted_dsts, extra, tx, unlock_time);
    THROW_WALLET_EXCEPTION_IF(!r, error::tx_not_constructed, m_currency.publicAddressBase58Prefix(), sources, splitted_dsts, unlock_time);
    uint64_t transactionSizeLimit = m_currency.blockGrantedFullRewardZone() * 125 / 100 - m_currency.minerTxBlobReservedSize();
    THROW_WALLET_EXCEPTION_IF(get_object_blobsize(tx) > transactionSizeLimit, error::tx_too_big, tx, transactionSizeLimit);

    std::string key_images;
    bool all_are_txin_to_key = std::all_of(tx.vin.begin(), tx.vin.end(), [&](const TransactionInput& s_e) -> bool
    {
      CHECKED_GET_SPECIFIC_VARIANT(s_e, const TransactionInputToKey, in, false);
      key_images += boost::to_string(in.keyImage) + " ";
      return true;
    });
    THROW_WALLET_EXCEPTION_IF(!all_are_txin_to_key, error::unexpected_txin_type, tx);

    ptx.key_images = key_images;
    ptx.fee = fee;
    ptx.dust = dust;
    ptx.tx = tx;
    ptx.change_dts = change_dts;
    ptx.selected_transfers = selected_transfers;  }
}
