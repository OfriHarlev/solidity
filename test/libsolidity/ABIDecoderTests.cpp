/*
	This file is part of solidity.

	solidity is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	solidity is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with solidity.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * Unit tests for Solidity's ABI decoder.
 */

#include <functional>
#include <string>
#include <tuple>
#include <boost/test/unit_test.hpp>
#include <libsolidity/interface/Exceptions.h>
#include <test/libsolidity/SolidityExecutionFramework.h>

#include <test/libsolidity/ABITestsCommon.h>

using namespace std;
using namespace std::placeholders;
using namespace dev::test;

namespace dev
{
namespace solidity
{
namespace test
{

BOOST_FIXTURE_TEST_SUITE(ABIDecoderTest, SolidityExecutionFramework)

BOOST_AUTO_TEST_CASE(BOTH_ENCODERS_macro)
{
	// This tests that the "both decoders macro" at least runs twice and
	// modifies the source.
	string sourceCode;
	int runs = 0;
	BOTH_ENCODERS(runs++;)
	BOOST_CHECK(sourceCode == NewEncoderPragma);
	BOOST_CHECK_EQUAL(runs, 2);
}

BOOST_AUTO_TEST_CASE(value_types)
{
	string sourceCode = R"(
		contract C {
			function f(uint a, uint16 b, uint24 c, int24 d, bytes3 x, bool e, C g) pure returns (uint) {
				if (a != 1) return 1;
				if (b != 2) return 2;
				if (c != 3) return 3;
				if (d != 4) return 4;
				if (x != "abc") return 5;
				if (e != true) return 6;
				if (g != this) return 7;
				return 20;
			}
		}
	)";
	BOTH_ENCODERS(
		compileAndRun(sourceCode);
		BOOST_CHECK(callContractFunction(
			"f(uint256,uint16,uint24,int24,bytes3,bool,address)",
			1, 2, 3, 4, string("abc"), true, u160(m_contractAddress)
		) == encodeArgs(u256(20)));
	)
}

BOOST_AUTO_TEST_CASE(enums)
{
	string sourceCode = R"(
		contract C {
			enum E { A, B }
			function f(E e) pure returns (uint x) {
				assembly { x := e }
			}
		}
	)";
	bool newDecoder = false;
	BOTH_ENCODERS(
		compileAndRun(sourceCode);
		BOOST_CHECK(callContractFunction("f(uint8)", 0) == encodeArgs(u256(0)));
		BOOST_CHECK(callContractFunction("f(uint8)", 1) == encodeArgs(u256(1)));
		// The old decoder was not as strict about enums
		BOOST_CHECK(callContractFunction("f(uint8)", 2) == (newDecoder ? encodeArgs() : encodeArgs(2)));
		BOOST_CHECK(callContractFunction("f(uint8)", u256(-1)) == (newDecoder? encodeArgs() : encodeArgs(u256(0xff))));
		newDecoder = true;
	)
}

BOOST_AUTO_TEST_CASE(cleanup)
{
	string sourceCode = R"(
		contract C {
			function f(uint16 a, int16 b, address c, bytes3 d, bool e)
					pure returns (uint v, uint w, uint x, uint y, uint z) {
				assembly { v := a  w := b x := c y := d z := e}
			}
		}
	)";
	BOTH_ENCODERS(
		compileAndRun(sourceCode);
		BOOST_CHECK(
			callContractFunction("f(uint16,int16,address,bytes3,bool)", 1, 2, 3, "a", true) ==
			encodeArgs(u256(1), u256(2), u256(3), string("a"), true)
		);
		BOOST_CHECK(
			callContractFunction(
				"f(uint16,int16,address,bytes3,bool)",
				u256(0xffffff), u256(0x1ffff), u256(-1), string("abcd"), u256(4)
			) ==
			encodeArgs(u256(0xffff), u256(-1), (u256(1) << 160) - 1, string("abc"), true)
		);
	)
}

BOOST_AUTO_TEST_CASE(fixed_arrays)
{
	string sourceCode = R"(
		contract C {
			function f(uint16[3] a, uint16[2][3] b, uint i, uint j, uint k)
					pure returns (uint, uint) {
				return (a[i], b[j][k]);
			}
		}
	)";
	BOTH_ENCODERS(
		compileAndRun(sourceCode);
		bytes args = encodeArgs(
			1, 2, 3,
			11, 12,
			21, 22,
			31, 32,
			1, 2, 1
		);
		BOOST_CHECK(
			callContractFunction("f(uint16[3],uint16[2][3],uint256,uint256,uint256)", args) ==
			encodeArgs(u256(2), u256(32))
		);
	)
}

BOOST_AUTO_TEST_CASE(dynamic_arrays)
{
	string sourceCode = R"(
		contract C {
			function f(uint a, uint16[] b, uint c)
					pure returns (uint, uint, uint) {
				return (b.length, b[a], c);
			}
		}
	)";
	BOTH_ENCODERS(
		compileAndRun(sourceCode);
		bytes args = encodeArgs(
			6, 0x60, 9,
			7,
			11, 12, 13, 14, 15, 16, 17
		);
		BOOST_CHECK(
			callContractFunction("f(uint256,uint16[],uint256)", args) ==
			encodeArgs(u256(7), u256(17), u256(9))
		);
	)
}

BOOST_AUTO_TEST_CASE(dynamic_nested_arrays)
{
	string sourceCode = R"(
		contract C {
			function f(uint a, uint16[][] b, uint[2][][3] c, uint d)
					pure returns (uint, uint, uint, uint, uint, uint) {
				return (b.length, b[1].length, b[1][1], c[1].length, c[1][1][1], d);
			}
			function test() {
				uint16[][] memory b = new uint16[][](3);
				b[0] = new uint16[](2);
				b[0][0] = 0x55;
				b[0][1] = 0x56;
				b[1] = new uint16[](4);
				b[1][0] = 0x65;
				b[1][1] = 0x66;
				b[1][2] = 0x67;
				b[1][3] = 0x68;

				uint[2][][3] memory c;
				c[0] = new uint[2][](1);
				c[0][0][1] = 0x75;
				c[1] = new uint[2][](5);
				c[1][1][1] = 0x85;

				this.f(0x12, b, c, 0x13);
			}
		}
	)";
	NEW_ENCODER(
		compileAndRun(sourceCode);
		BOOST_CHECK(
			callContractFunction("test()") ==
			encodeArgs(u256(0x12), u256(4), 0x66, 5, 0x85, 0x13)
		);
	)
}

BOOST_AUTO_TEST_CASE(byte_arrays)
{
	string sourceCode = R"(
		contract C {
			function f(uint a, bytes b, uint c) public
					pure returns (uint, uint, byte, uint) {
				return (a, b.length, b[3], c);
			}

			function f_external(uint a, bytes b, uint c) external
					pure returns (uint, uint, byte, uint) {
				return (a, b.length, b[3], c);
			}
		}
	)";
	int enc= 0;
	BOTH_ENCODERS(
		compileAndRun(sourceCode);
		bytes args = encodeArgs(
			6, 0x60, 9,
			7, "abcdefg"
		);
	cout << enc++ << endl;
	cout << toHexAndSplit(callContractFunction("f(uint256,bytes,uint256)", args)) << endl;
		BOOST_CHECK(
			callContractFunction("f(uint256,bytes,uint256)", args) ==
			encodeArgs(u256(6), u256(7), 'd')
		);
		BOOST_CHECK(
			callContractFunction("f_external(uint256,bytes,uint256)", args) ==
			encodeArgs(u256(6), u256(7), 'd')
		);
	)
}

// dynamic nested arrays

// calldata types
// decoding from memory (used for constructor, especially forwarding to base constructor)
// test about incorrect input length
// test cleanup inside arrays and structs
// test decoding storage pointers

// btye arrays

// test that calldata types are decoded efficiently, i.e. no element access for bytes,
// and perhaps also no element access for uint256

// TODO: For decoding from memory, we might want to avoid a copy. Could that pose a problem?
// TODO: ridiculously sized arrays, also when the size comes from deeply nested "short" arrays

// check again in the code that "offset" is always compared to "end"

// structs
// combination of structs, arrays and value types

BOOST_AUTO_TEST_SUITE_END()

}
}
} // end namespaces
