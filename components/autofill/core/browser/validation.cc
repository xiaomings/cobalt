// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill/core/browser/validation.h"

#include <stddef.h>

#include <ostream>

#include "base/check.h"
#include "base/containers/adapters.h"
#include "base/notreached.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_piece.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "components/autofill/core/browser/autofill_data_util.h"
#include "components/autofill/core/browser/data_model/credit_card.h"
#include "components/autofill/core/browser/geo/phone_number_i18n.h"
#include "components/autofill/core/browser/geo/state_names.h"
#include "components/autofill/core/common/autofill_clock.h"
#include "components/autofill/core/common/autofill_regex_constants.h"
#include "components/autofill/core/common/autofill_regexes.h"
#include "components/strings/grit/components_strings.h"
#include "ui/base/l10n/l10n_util.h"

namespace autofill {

bool IsValidCreditCardExpirationDate(int year,
                                     int month,
                                     const base::Time& now) {
  if (month < 1 || month > 12)
    return false;

  base::Time::Exploded now_exploded;
  now.LocalExplode(&now_exploded);

  if (year < now_exploded.year)
    return false;

  if (year == now_exploded.year && month < now_exploded.month)
    return false;

  return true;
}

bool IsValidCreditCardExpirationYear(int year, const base::Time& now) {
  base::Time::Exploded now_exploded;
  now.LocalExplode(&now_exploded);

  return year >= now_exploded.year;
}

bool IsValidCreditCardNumber(const std::u16string& text) {
  const std::u16string number = CreditCard::StripSeparators(text);

  if (!HasCorrectLength(number))
    return false;

  return PassesLuhnCheck(number);
}

bool HasCorrectLength(const std::u16string& number) {
  // Credit card numbers are at most 19 digits in length, 12 digits seems to
  // be a fairly safe lower-bound [1].  Specific card issuers have more rigidly
  // defined sizes.
  // (Last updated: May 29, 2017)
  // [1] https://en.wikipedia.org/wiki/Payment_card_number.
  // CardEditor.isCardNumberLengthMaxium() needs to be kept in sync.
  const char* const type = CreditCard::GetCardNetwork(number);
  if (type == kAmericanExpressCard && number.size() != 15)
    return false;
  if (type == kDinersCard && number.size() != 14)
    return false;
  if (type == kDiscoverCard && number.size() != 16)
    return false;
  if (type == kEloCard && number.size() != 16)
    return false;
  if (type == kJCBCard && number.size() != 16)
    return false;
  if (type == kMasterCard && number.size() != 16)
    return false;
  if (type == kMirCard && number.size() != 16)
    return false;
  if (type == kTroyCard && number.size() != 16)
    return false;
  if (type == kUnionPay && (number.size() < 16 || number.size() > 19))
    return false;
  if (type == kVisaCard && number.size() != 13 && number.size() != 16 &&
      number.size() != 19)
    return false;
  if (type == kGenericCard && (number.size() < 12 || number.size() > 19))
    return false;

  return true;
}

bool PassesLuhnCheck(const std::u16string& number) {
  // Use the Luhn formula [3] to validate the number.
  // [3] http://en.wikipedia.org/wiki/Luhn_algorithm
  int sum = 0;
  bool odd = false;
  for (char c : base::Reversed(number)) {
    if (!base::IsAsciiDigit(c))
      return false;

    int digit = c - '0';
    if (odd) {
      digit *= 2;
      sum += digit / 10 + digit % 10;
    } else {
      sum += digit;
    }
    odd = !odd;
  }

  return (sum % 10) == 0;
}

bool IsValidCreditCardSecurityCode(const std::u16string& code,
                                   const base::StringPiece card_network,
                                   CvcType cvc_type) {
  return code.length() == GetCvcLengthForCardNetwork(card_network, cvc_type) &&
         base::ContainsOnlyChars(code, u"0123456789");
}

bool IsValidCreditCardNumberForBasicCardNetworks(
    const std::u16string& text,
    const std::set<std::string>& supported_basic_card_networks,
    std::u16string* error_message) {
  DCHECK(error_message);

  // The type check is cheaper than the credit card number check.
  const std::string basic_card_issuer_network =
      data_util::GetPaymentRequestData(CreditCard::GetCardNetwork(text))
          .basic_card_issuer_network;
  if (!supported_basic_card_networks.count(basic_card_issuer_network)) {
    *error_message = l10n_util::GetStringUTF16(
        IDS_PAYMENTS_VALIDATION_UNSUPPORTED_CREDIT_CARD_TYPE);
    return false;
  }

  if (IsValidCreditCardNumber(text))
    return true;

  *error_message = l10n_util::GetStringUTF16(
      IDS_PAYMENTS_CARD_NUMBER_INVALID_VALIDATION_MESSAGE);
  return false;
}

bool IsValidEmailAddress(const std::u16string& text) {
  // E-Mail pattern as defined by the WhatWG. (4.10.7.1.5 E-Mail state)
  static constexpr char16_t kEmailPattern[] =
      u"^[a-zA-Z0-9.!#$%&'*+/=?^_`{|}~-]+@[a-zA-Z0-9-]+(?:\\.[a-zA-Z0-9-]+)*$";
  return MatchesRegex<kEmailPattern>(text);
}

bool IsValidState(const std::u16string& text) {
  return !state_names::GetAbbreviationForName(text).empty() ||
         !state_names::GetNameForAbbreviation(text).empty();
}

bool IsPossiblePhoneNumber(const std::u16string& text,
                           const std::string& country_code) {
  return i18n::IsPossiblePhoneNumber(base::UTF16ToUTF8(text), country_code);
}

bool IsValidZip(const std::u16string& text) {
  static constexpr char16_t kZipPattern[] = u"^\\d{5}(-\\d{4})?$";
  return MatchesRegex<kZipPattern>(text);
}

bool IsSSN(const std::u16string& text) {
  std::u16string number_string;
  base::RemoveChars(text, u"- ", &number_string);

  // A SSN is of the form AAA-GG-SSSS (A = area number, G = group number, S =
  // serial number). The validation we do here is simply checking if the area,
  // group, and serial numbers are valid.
  //
  // Historically, the area number was assigned per state, with the group number
  // ascending in an alternating even/odd sequence. With that scheme it was
  // possible to check for validity by referencing a table that had the highest
  // group number assigned for a given area number. (This was something that
  // Chromium never did though, because the "high group" values were constantly
  // changing.)
  //
  // However, starting on 25 June 2011 the SSA began issuing SSNs randomly from
  // all areas and groups. Group numbers and serial numbers of zero remain
  // invalid, and areas 000, 666, and 900-999 remain invalid.
  //
  // References for current practices:
  //   http://www.socialsecurity.gov/employer/randomization.html
  //   http://www.socialsecurity.gov/employer/randomizationfaqs.html
  //
  // References for historic practices:
  //   http://www.socialsecurity.gov/history/ssn/geocard.html
  //   http://www.socialsecurity.gov/employer/stateweb.htm
  //   http://www.socialsecurity.gov/employer/ssnvhighgroup.htm

  if (number_string.length() != 9 || !base::IsStringASCII(number_string))
    return false;

  int area;
  if (!base::StringToInt(base::MakeStringPiece16(number_string.begin(),
                                                 number_string.begin() + 3),
                         &area)) {
    return false;
  }
  if (area < 1 || area == 666 || area >= 900) {
    return false;
  }

  int group;
  if (!base::StringToInt(base::MakeStringPiece16(number_string.begin() + 3,
                                                 number_string.begin() + 5),
                         &group) ||
      group == 0) {
    return false;
  }

  int serial;
  if (!base::StringToInt(base::MakeStringPiece16(number_string.begin() + 5,
                                                 number_string.begin() + 9),
                         &serial) ||
      serial == 0) {
    return false;
  }

  return true;
}

bool IsValidForType(const std::u16string& value,
                    ServerFieldType type,
                    std::u16string* error_message) {
  switch (type) {
    case CREDIT_CARD_NAME_FULL:
      if (!value.empty())
        return true;

      if (error_message) {
        *error_message =
            l10n_util::GetStringUTF16(IDS_PAYMENTS_VALIDATION_INVALID_NAME);
      }
      break;

    case CREDIT_CARD_EXP_MONTH: {
      CreditCard temp;
      // Expiration month was in an invalid format.
      temp.SetExpirationMonthFromString(value, /* app_locale= */ std::string());
      if (temp.expiration_month() == 0) {
        if (error_message) {
          *error_message = l10n_util::GetStringUTF16(
              IDS_PAYMENTS_VALIDATION_INVALID_CREDIT_CARD_EXPIRATION_MONTH);
        }
        break;
      }
      return true;
    }

    case CREDIT_CARD_EXP_2_DIGIT_YEAR:
    case CREDIT_CARD_EXP_4_DIGIT_YEAR: {
      CreditCard temp;
      temp.SetExpirationYearFromString(value);
      // Expiration year was in an invalid format.
      if ((temp.expiration_year() == 0) ||
          (type == CREDIT_CARD_EXP_2_DIGIT_YEAR && value.size() != 2u) ||
          (type == CREDIT_CARD_EXP_4_DIGIT_YEAR && value.size() != 4u)) {
        if (error_message) {
          *error_message = l10n_util::GetStringUTF16(
              IDS_PAYMENTS_VALIDATION_INVALID_CREDIT_CARD_EXPIRATION_YEAR);
        }
        break;
      }

      base::Time::Exploded now_exploded;
      AutofillClock::Now().LocalExplode(&now_exploded);
      if (temp.expiration_year() >= now_exploded.year)
        return true;

      // If the year is before this year, it's expired.
      if (error_message) {
        *error_message = l10n_util::GetStringUTF16(
            IDS_PAYMENTS_VALIDATION_INVALID_CREDIT_CARD_EXPIRED);
      }
      break;
    }

    case CREDIT_CARD_EXP_DATE_2_DIGIT_YEAR:
    case CREDIT_CARD_EXP_DATE_4_DIGIT_YEAR: {
      static constexpr char16_t kDateYY[] = u"^[0-9]{1,2}[-/|]?[0-9]{2}$";
      static constexpr char16_t kDateYYYY[] = u"^[0-9]{1,2}[-/|]?[0-9]{4}$";

      CreditCard temp;
      temp.SetExpirationDateFromString(value);

      // Expiration date was in an invalid format.
      if (temp.expiration_month() == 0 || temp.expiration_year() == 0 ||
          (type == CREDIT_CARD_EXP_DATE_2_DIGIT_YEAR
               ? !MatchesRegex<kDateYY>(value)
               : !MatchesRegex<kDateYYYY>(value))) {
        if (error_message) {
          *error_message = l10n_util::GetStringUTF16(
              IDS_PAYMENTS_CARD_EXPIRATION_INVALID_VALIDATION_MESSAGE);
        }
        break;
      }

      // Checking for card expiration.
      if (IsValidCreditCardExpirationDate(temp.expiration_year(),
                                          temp.expiration_month(),
                                          AutofillClock::Now())) {
        return true;
      }

      if (error_message) {
        *error_message = l10n_util::GetStringUTF16(
            IDS_PAYMENTS_VALIDATION_INVALID_CREDIT_CARD_EXPIRED);
      }
      break;
    }

    case CREDIT_CARD_NUMBER:
      NOTREACHED() << "IsValidCreditCardNumberForBasicCardNetworks should be "
                   << "used to validate credit card numbers";
      break;

    default:
      // Other types such as CREDIT_CARD_TYPE and CREDIT_CARD_VERIFICATION_CODE
      // are not validated for now.
      NOTREACHED() << "Attempting to validate unsupported type " << type;
      break;
  }
  return false;
}

size_t GetCvcLengthForCardNetwork(const base::StringPiece card_network,
                                  CvcType cvc_type) {
  if (card_network == kAmericanExpressCard &&
      cvc_type == CvcType::kRegularCvc) {
    return AMEX_CVC_LENGTH;
  }

  return GENERAL_CVC_LENGTH;
}

bool IsUPIVirtualPaymentAddress(const std::u16string& value) {
  return MatchesRegex<kUPIVirtualPaymentAddressRe>(value);
}

bool IsInternationalBankAccountNumber(const std::u16string& value) {
  std::u16string no_spaces;
  base::RemoveChars(value, u" ", &no_spaces);
  return MatchesRegex<kInternationalBankAccountNumberValueRe>(no_spaces);
}

bool IsPlausibleCreditCardCVCNumber(const std::u16string& value) {
  return MatchesRegex<kCreditCardCVCPattern>(value);
}

bool IsPlausible4DigitExpirationYear(const std::u16string& value) {
  return MatchesRegex<kCreditCard4DigitExpYearPattern>(value);
}
}  // namespace autofill
