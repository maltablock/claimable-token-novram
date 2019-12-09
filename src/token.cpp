/**
 *  @file
 *  @copyright defined in LICENSE
 */

#include "token.hpp"

namespace eosio {

void token::create( name   issuer,
                    asset  maximum_supply )
{
    require_auth( _self );

    auto sym = maximum_supply.symbol;
    check( sym.is_valid(), "invalid symbol name" );
    check( maximum_supply.is_valid(), "invalid supply");
    check( maximum_supply.amount > 0, "max-supply must be positive");

    stats statstable( _self, sym.code().raw() );
    auto existing = statstable.find( sym.code().raw() );
    check( existing == statstable.end(), "token with symbol already exists" );

    statstable.emplace( _self, [&]( auto& s ) {
       s.supply.symbol = maximum_supply.symbol;
       s.max_supply    = maximum_supply;
       s.issuer        = issuer;
    });
}


void token::update( name  issuer,
                    asset maximum_supply )
{
    require_auth( _self );

    auto sym = maximum_supply.symbol;
    check( sym.is_valid(), "invalid symbol name" );
    check( maximum_supply.is_valid(), "invalid supply");
    check( maximum_supply.amount > 0, "max-supply must be positive");

    stats statstable( _self, sym.code().raw() );
    auto existing = statstable.find( sym.code().raw() );
    check( existing != statstable.end(), "token with symbol does not exist, create token before update" );
    const auto& st = *existing;

    check( st.supply.amount <= maximum_supply.amount, "max-supply cannot be less than available supply");
    check( maximum_supply.symbol == st.supply.symbol, "symbol precision mismatch" );

    statstable.modify( st, same_payer, [&]( auto& s ) {
      s.max_supply    = maximum_supply;
      s.issuer        = issuer;
    });
}



void token::issue( const name& to, const asset& quantity, const string& memo )
{
    auto sym = quantity.symbol;
    check( sym.is_valid(), "invalid symbol name" );
    check( memo.size() <= 256, "memo has more than 256 bytes" );

    stats statstable( get_self(), sym.code().raw() );
    auto existing = statstable.find( sym.code().raw() );
    check( existing != statstable.end(), "token with symbol does not exist, create token before issue" );
    const auto& st = *existing;
    check( to == st.issuer, "tokens can only be issued to issuer account" );

    require_auth( st.issuer );
    check( quantity.is_valid(), "invalid quantity" );
    check( quantity.amount > 0, "must issue positive quantity" );

    check( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );
    check( quantity.amount <= st.max_supply.amount - st.supply.amount, "quantity exceeds available supply");

    statstable.modify( st, same_payer, [&]( auto& s ) {
       s.supply += quantity;
    });

    add_balance( st.issuer, quantity, st.issuer, true );
}


void token::burn( name from, asset quantity )
{
    auto sym = quantity.symbol;
    check( sym.is_valid(), "invalid symbol name" );
    
    stats statstable( _self, sym.code().raw() );
    auto existing = statstable.find( sym.code().raw() );
    check( existing != statstable.end(), "token with symbol does not exist, create token before burn" );
    const auto& st = *existing;

    require_auth( st.issuer );
    check( quantity.is_valid(), "invalid quantity" );
    check( quantity.amount > 0, "must issue positive quantity" );

    check( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );

    statstable.modify( st, same_payer, [&]( auto& s ) {
       s.supply -= quantity;
    });

    sub_balance( from, quantity );
}

void token::transfer( const name&    from,
                      const name&    to,
                      const asset&   quantity,
                      const string&  memo )
{
    check( from != to, "cannot transfer to self" );
    require_auth( from );

    check( is_account( to ), "to account does not exist");
    auto sym = quantity.symbol.code();
    stats statstable( _self, sym.raw() );
    const auto& st = statstable.get( sym.raw() );

    require_recipient( from );
    require_recipient( to );

    check( quantity.is_valid(), "invalid quantity" );
    check( quantity.amount > 0, "must transfer positive quantity" );
    check( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );
    check( memo.size() <= 256, "memo has more than 256 bytes" );

    do_claim( from, quantity.symbol, from );
    sub_balance( from, quantity );
    add_balance( to, quantity, from, from != st.issuer );

    //account needs to exist first, dont auto claim when issuer
    if(from != st.issuer) {
      do_claim( to, quantity.symbol, from );
    }
}



void token::claim( name owner, const symbol& sym ) {
  do_claim(owner,sym,owner);
}

void token::do_claim( name owner, const symbol& sym, name payer ) {
  check( sym.is_valid(), "invalid symbol name" );
  auto sym_code_raw = sym.code().raw();

  require_auth( payer );
  accounts owner_acnts( _self, owner.value );

  const auto& existing = owner_acnts.get( sym_code_raw, "no balance object found" );
  if( !existing.claimed ) {
    //save the balance
    auto value = existing.balance;
    //erase the table freeing ram to the issuer
    owner_acnts.erase( existing );
    //create a new index
    auto replace = owner_acnts.find( sym_code_raw );
    //confirm there are definitely no balance now
    check(replace == owner_acnts.end(), "there must be no balance object" );
    //add the new claimed balance paid by owner
    owner_acnts.emplace( payer, [&]( auto& a ){
      a.balance = value;
      a.claimed = true;
    });
  }
}

void token::recover( name owner, const symbol& sym ) {
  auto sym_code_raw = sym.code().raw();

  stats statstable( _self, sym_code_raw );
  auto existing = statstable.find( sym_code_raw );
  check( existing != statstable.end(), "token with symbol does not exist, create token before issue" );
  const auto& st = *existing;

  require_auth( st.issuer );

  //fail gracefully so we dont have to take another snapshot
  accounts owner_acnts( _self, owner.value );
  auto owned = owner_acnts.find( sym_code_raw );
  if( owned != owner_acnts.end() ) {
    if( !owned->claimed ) {
      sub_balance( owner, owned->balance );
      add_balance( st.issuer, owned->balance, st.issuer, true );
    }
  }
}


void token::open( const name& owner, const symbol& symbol, const name& ram_payer )
{
   require_auth( ram_payer );

   check( is_account( owner ), "owner account does not exist" );

   auto sym_code_raw = symbol.code().raw();
   stats statstable( get_self(), sym_code_raw );
   const auto& st = statstable.get( sym_code_raw, "symbol does not exist" );
   check( st.supply.symbol == symbol, "symbol precision mismatch" );

   accounts acnts( get_self(), owner.value );
   auto it = acnts.find( sym_code_raw );
   if( it == acnts.end() ) {
      acnts.emplace( ram_payer, [&]( auto& a ){
        a.balance = asset{0, symbol};
        a.claimed = true;
      });
   }
}

void token::close( const name& owner, const symbol& symbol )
{
   require_auth( owner );
   accounts acnts( get_self(), owner.value );
   auto it = acnts.find( symbol.code().raw() );
   check( it != acnts.end(), "Balance row already deleted or never existed. Action won't have any effect." );
   check( it->balance.amount == 0, "Cannot close because the balance is not zero." );
   acnts.erase( it );
}

void token::sub_balance( name owner, asset value ) {
  auto sym_code_raw = value.symbol.code().raw();
  accounts from_acnts( _self, owner.value );

  const auto& from = from_acnts.get( sym_code_raw, "no balance object found" );
  check( from.balance.amount >= value.amount, "overdrawn balance" );

  if( from.balance.amount == value.amount ) {
    from_acnts.erase( from );
  } else {
    from_acnts.modify( from, owner, [&]( auto& a ) {
        a.balance -= value;
    });
  }
}

void token::add_balance( name owner, asset value, name ram_payer, bool claimed )
{
  accounts to_acnts( _self, owner.value );
  auto to = to_acnts.find( value.symbol.code().raw() );

  if( to == to_acnts.end() ) {
    to_acnts.emplace( ram_payer, [&]( auto& a ){
      a.balance = value;
      a.claimed = claimed;
    });
  } else {
    to_acnts.modify( to, same_payer, [&]( auto& a ) {
      a.balance += value;
    });
  }
}

} /// namespace eosio

EOSIO_DISPATCH(eosio::token, (create)(update)(issue)(transfer)(claim)(recover)(burn)(open) )
