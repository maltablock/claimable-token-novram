/**
 *  @file
 *  @copyright defined in LICENSE
 */
#pragma once

#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>


namespace eosio {

   using std::string;

   CONTRACT token : public contract {

      public:
         using contract::contract;

         ACTION create( name issuer,
                      asset maximum_supply);

         ACTION update( name issuer,
                      asset maximum_supply);

         ACTION issue( const name& to, const asset& quantity, const string& memo );
         ACTION burn( name from, asset quantity );
         
         ACTION claim( name owner, const symbol& sym );
         
         ACTION recover( name owner, const symbol& sym );
         
         ACTION transfer( const name&    from,
                      const name&    to,
                      const asset&   quantity,
                      const string&  memo );

         ACTION open( const name& owner, const symbol& symbol, const name& ram_payer );
         ACTION close( const name& owner, const symbol& symbol );

         static asset get_supply( name token_contract_account, symbol_code sym_code )
         {
            stats statstable( token_contract_account, sym_code.raw() );
            const auto& st = statstable.get( sym_code.raw() );
            return st.supply;
         }

         static asset get_balance( name token_contract_account, name owner, symbol_code sym_code )
         {
            accounts accountstable( token_contract_account, owner.value );
            const auto& ac = accountstable.get( sym_code.raw() );
            return ac.balance;
         }

      private:
         TABLE account {
            asset    balance;
            bool     claimed = false;

            uint64_t primary_key()const { return balance.symbol.code().raw(); }
         };

         TABLE currency_stats {
            asset    supply;
            asset    max_supply;
            name     issuer;

            uint64_t primary_key()const { return supply.symbol.code().raw(); }
         };

         typedef eosio::multi_index< "accounts"_n, account> accounts;
         typedef eosio::multi_index< "stat"_n, currency_stats> stats;
         
         void sub_balance( name owner, asset value );
         void add_balance( name owner, asset value, name ram_payer, bool claimed );
         void do_claim( name owner, const symbol& sym, name payer );
   };

} /// namespace eosio
