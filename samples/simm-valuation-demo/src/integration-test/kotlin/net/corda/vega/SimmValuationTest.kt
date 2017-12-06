package net.corda.vega

import com.opengamma.strata.product.common.BuySell
import net.corda.core.identity.CordaX500Name
import net.corda.core.utilities.getOrThrow
import net.corda.testing.*
import net.corda.testing.driver.driver
import net.corda.testing.http.HttpApi
import net.corda.vega.api.PortfolioApi
import net.corda.vega.api.PortfolioApiUtils
import net.corda.vega.api.SwapDataModel
import net.corda.vega.api.SwapDataView
import org.assertj.core.api.Assertions.assertThat
import org.junit.ClassRule
import org.junit.Test
import java.math.BigDecimal
import java.time.LocalDate

class SimmValuationTest : IntegrationTest() {
    private companion object {
        // SIMM demo can only currently handle one valuation date due to a lack of market data or a market data source.
        val valuationDate: LocalDate = LocalDate.parse("2016-06-06")
        val nodeALegalName = DUMMY_BANK_A.name
        val nodeBLegalName = DUMMY_BANK_B.name
        val testTradeId = "trade1"

        @ClassRule @JvmField
        val databaseSchemas = IntegrationTestSchemas(*listOf(DUMMY_BANK_A, DUMMY_BANK_B, DUMMY_NOTARY)
                .map { it.toDatabaseSchemaName() }.toTypedArray())
    }

    @Test
    fun `runs SIMM valuation demo`() {
        driver(isDebug = true, extraCordappPackagesToScan = listOf("net.corda.vega.contracts")) {
            val nodeAFuture = startNode(providedName = nodeALegalName)
            val nodeBFuture = startNode(providedName = nodeBLegalName)
            val (nodeA, nodeB) = listOf(nodeAFuture, nodeBFuture).map { it.getOrThrow() }
            val nodeAWebServerFuture = startWebserver(nodeA)
            val nodeBWebServerFuture = startWebserver(nodeB)
            val nodeAApi = HttpApi.fromHostAndPort(nodeAWebServerFuture.getOrThrow().listenAddress, "api/simmvaluationdemo")
            val nodeBApi = HttpApi.fromHostAndPort(nodeBWebServerFuture.getOrThrow().listenAddress, "api/simmvaluationdemo")
            val nodeBParty = getPartyWithName(nodeAApi, nodeBLegalName)
            val nodeAParty = getPartyWithName(nodeBApi, nodeALegalName)

            createTradeBetween(nodeAApi, nodeBParty, testTradeId)
            assertTradeExists(nodeBApi, nodeAParty, testTradeId)
            assertTradeExists(nodeAApi, nodeBParty, testTradeId)
            runValuationsBetween(nodeAApi, nodeBParty)
            assertValuationExists(nodeBApi, nodeAParty)
            assertValuationExists(nodeAApi, nodeBParty)
        }
    }

    private fun getPartyWithName(partyApi: HttpApi, counterparty: CordaX500Name): PortfolioApi.ApiParty {
        return getAvailablePartiesFor(partyApi).counterparties.single { it.text == counterparty }
    }

    private fun getAvailablePartiesFor(partyApi: HttpApi): PortfolioApi.AvailableParties {
        return partyApi.getJson("whoami")
    }

    private fun createTradeBetween(partyApi: HttpApi, counterparty: PortfolioApi.ApiParty, tradeId: String) {
        val trade = SwapDataModel(tradeId, "desc", valuationDate, "EUR_FIXED_1Y_EURIBOR_3M",
                valuationDate, LocalDate.parse("2020-01-02"), BuySell.BUY, BigDecimal.valueOf(1000), BigDecimal.valueOf(0.1))
        partyApi.putJson("${counterparty.id}/trades", trade)
    }

    private fun assertTradeExists(partyApi: HttpApi, counterparty: PortfolioApi.ApiParty, tradeId: String) {
        val trades = partyApi.getJson<Array<SwapDataView>>("${counterparty.id}/trades")
        assertThat(trades).filteredOn { it.id == tradeId }.isNotEmpty()
    }

    private fun runValuationsBetween(partyApi: HttpApi, counterparty: PortfolioApi.ApiParty) {
        partyApi.postJson("${counterparty.id}/portfolio/valuations/calculate", PortfolioApi.ValuationCreationParams(valuationDate))
    }

    private fun assertValuationExists(partyApi: HttpApi, counterparty: PortfolioApi.ApiParty) {
        val valuations = partyApi.getJson<PortfolioApiUtils.ValuationsView>("${counterparty.id}/portfolio/valuations")
        assertThat(valuations.initialMargin.call["total"]).isNotEqualTo(0.0)
    }
}
