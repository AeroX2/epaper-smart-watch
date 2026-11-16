package com.james.epaperwatch.bridge

import org.junit.Assert.assertEquals
import org.junit.Test

class OtpRedactorTest {
    private fun same(text: String, context: String = "", before: Boolean = true) =
        assertEquals(text, OtpRedactor.redact(text, context, before))

    // ---- must be masked ----

    @Test fun plainCode() =
        assertEquals("Your code is ******", OtpRedactor.redact("Your code is 123456"))

    @Test fun splitCodeBeforeKeyword() =
        assertEquals(
            "****** is your verification code",
            OtpRedactor.redact("123 456 is your verification code"),
        )

    @Test fun hyphenatedCode() =
        assertEquals("Your OTP: ******", OtpRedactor.redact("Your OTP: 123-456"))

    @Test fun prefixedCodeAlone() = assertEquals("G-******", OtpRedactor.redact("G-123456"))

    @Test fun prefixedCodeInSentence() =
        assertEquals(
            "G-****** is your Google verification code.",
            OtpRedactor.redact("G-123456 is your Google verification code."),
        )

    @Test fun keywordAfterCode() =
        assertEquals(
            "Use ****** to verify your account",
            OtpRedactor.redact("Use 482913 to verify your account"),
        )

    @Test fun shortPin() = assertEquals("PIN: ****", OtpRedactor.redact("PIN: 4821"))

    @Test fun codeOnNextLine() =
        assertEquals(
            "Your verification code is:\n******",
            OtpRedactor.redact("Your verification code is:\n482913"),
        )

    @Test fun keywordOnlyInTitle() =
        assertEquals("******", OtpRedactor.redact("482913", "Your verification code", true))

    @Test fun keywordOnlyInBodyCodeInTitle() =
        assertEquals("******", OtpRedactor.redact("482913", "is your login code", false))

    @Test fun bankMessageKeepsAmountMasksCode() =
        assertEquals(
            "Your OTP for transaction of Rs. 5000 at STORE is ******. Valid for 10 min",
            OtpRedactor.redact("Your OTP for transaction of Rs. 5000 at STORE is 482913. Valid for 10 min"),
        )

    @Test fun codeMaskedButPriceYearAndPhoneKept() =
        assertEquals(
            "Your code is ******, valid until 14 March 2026. Call 0412 345 678 for help.",
            OtpRedactor.redact(
                "Your code is 482913, valid until 14 March 2026. Call 0412 345 678 for help.",
            ),
        )

    @Test fun codeMaskedAmountKept() =
        assertEquals(
            "Enter code ****** to confirm payment of \$2500",
            OtpRedactor.redact("Enter code 482913 to confirm payment of \$2500"),
        )

    // ---- must be left alone ----

    @Test fun noKeyword() = same("Meeting moved to room 2025, bring 1234 flyers")

    @Test fun yearNextToMonth() = same("Your code expires on March 12, 2026")

    @Test fun yearInNumericDate() = same("New sign-in on 12/03/2026 from Chrome")

    @Test fun isoDate() = same("Login from a new device on 2026-03-12")

    @Test fun priceWithSymbol() = same("Use code SAVE and pay \$1299 today")

    @Test fun priceWithDecimals() = same("Promo code applied: total 1234.50")

    @Test fun priceWithCurrencyCode() = same("Discount code used, you paid 2500 USD")

    @Test fun phoneNumberWithGroups() = same("Trouble with your login? Call 1800 123 456")

    @Test fun internationalPhoneNumber() = same("Verify by calling +61 412 345 678")

    @Test fun otherSentence() = same("Your login was successful. Order 123456 has shipped")

    @Test fun keywordTooFarAway() =
        same("The code word for the surprise party at the big old house on the hill tonight is 4821")

    @Test fun contextKeywordButNumberFarIntoText() =
        same(
            "Thanks for shopping with us today. We hope to see you again soon. Order 123456",
            "Your login code",
            true,
        )

    @Test fun longDigitRunIsNotACode() = same("Your code reference 1234567890")

    @Test fun emptyText() = same("")
}
