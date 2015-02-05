/* Copyright (C) 2013-2014 Christian Fischbach <cf@cflib.de>
 *
 * This file is part of cflib.
 *
 * cflib is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * cflib is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with cflib. If not, see <http://www.gnu.org/licenses/>.
 */

#include <cflib/crypt/util.h>
#include <cflib/util/test.h>

using namespace cflib::crypt;


#include <botan/auto_rng.h>
#include <botan/hex.h>
#include <botan/pkcs10.h>
#include <botan/rsa.h>
#include <botan/tls_client.h>
#include <botan/tls_server.h>
#include <botan/x509_ca.h>
#include <botan/x509self.h>

#include <iostream>
#include <vector>
#include <memory>

using namespace Botan;

namespace {

class Credentials_Manager_Test : public Botan::Credentials_Manager
{
public:
	Credentials_Manager_Test(const X509_Certificate & server_cert, const X509_Certificate & ca_cert,
		Private_Key * server_key)
	:
		m_server_cert(server_cert),
		m_ca_cert(ca_cert),
		m_key(server_key)
	{
		auto store = new Certificate_Store_In_Memory;
		store->add_certificate(m_ca_cert);
		m_stores.push_back(store);
	}

	std::vector<Certificate_Store *> trusted_certificate_authorities(const std::string &, const std::string &)
	{
		return m_stores;
	}

	std::vector<X509_Certificate> cert_chain(
		const std::vector<std::string> & cert_key_types,
		const std::string & type,
		const std::string &)
	{
		std::vector<X509_Certificate> chain;

		if (type == "tls-server") {
			bool have_match = false;
			for (size_t i = 0 ; i != cert_key_types.size() ; ++i)
				if (cert_key_types[i] == m_key->algo_name()) have_match = true;

			if (have_match) {
				chain.push_back(m_server_cert);
				chain.push_back(m_ca_cert);
			}
		}

		return chain;
	}

	void verify_certificate_chain(
		const std::string & type,
		const std::string & purported_hostname,
		const std::vector<Botan::X509_Certificate> & cert_chain)
	{
		try {
			Credentials_Manager::verify_certificate_chain(type, purported_hostname, cert_chain);
		} catch (std::exception & e) {
			std::cout << "Certificate verification failed - " << e.what() << " - but will ignore\n";
		}
	}

	Private_Key * private_key_for(const X509_Certificate &, const std::string &, const std::string &)
	{
		return m_key;
	}

public:
	X509_Certificate m_server_cert, m_ca_cert;
	Private_Key * m_key;
	std::vector<Certificate_Store *> m_stores;
};

Credentials_Manager * create_creds(RandomNumberGenerator & rng)
{
	std::auto_ptr<Private_Key> ca_key(new RSA_PrivateKey(rng, 1024));

	X509_Cert_Options ca_opts;
	ca_opts.common_name = "Test CA";
	ca_opts.country = "US";
	ca_opts.CA_key(1);

	X509_Certificate ca_cert = X509::create_self_signed_cert(ca_opts, *ca_key, "SHA-256", rng);

	Private_Key * server_key = new RSA_PrivateKey(rng, 1024);

	X509_Cert_Options server_opts;
	server_opts.common_name = "localhost";
	server_opts.country = "US";

	PKCS10_Request req = X509::create_cert_req(server_opts, *server_key, "SHA-256", rng);

	X509_CA ca(ca_cert, *ca_key, "SHA-256");

	auto now = std::chrono::system_clock::now();
	X509_Time start_time(now);
	typedef std::chrono::duration<int, std::ratio<31556926>> years;
	X509_Time end_time(now + years(1));

	X509_Certificate server_cert = ca.sign_request(req, rng, start_time, end_time);

	return new Credentials_Manager_Test(server_cert, ca_cert, server_key);
}

bool test_handshake(RandomNumberGenerator & rng,
	TLS::Protocol_Version offer_version,
	Credentials_Manager & creds,
	TLS::Policy & policy)
{
	TLS::Session_Manager_In_Memory server_sessions(rng);
	TLS::Session_Manager_In_Memory client_sessions(rng);

	std::vector<byte> c2s_q, s2c_q, c2s_data, s2c_data;

	auto handshake_complete = [&](const TLS::Session& session) -> bool
	{
		if (session.version() != offer_version)
			std::cout << "Wrong version negotiated\n";
		return true;
	};

	auto print_alert = [&](TLS::Alert alert, const byte[], size_t)
	{
		if (alert.is_valid())
			std::cout << "Server recvd alert " << alert.type_string() << "\n";
	};

	auto save_server_data = [&](const byte buf[], size_t sz)
	{
		c2s_data.insert(c2s_data.end(), buf, buf + sz);
	};

	auto save_client_data = [&](const byte buf[], size_t sz)
	{
		s2c_data.insert(s2c_data.end(), buf, buf + sz);
	};

	TLS::Server server(
		[&](const byte buf[], size_t sz) { s2c_q.insert(s2c_q.end(), buf, buf + sz); },
		save_server_data,
		print_alert,
		handshake_complete,
		server_sessions,
		creds,
		policy,
		rng,
		{ "test/1", "test/2" });

	auto next_protocol_chooser = [&](std::vector<std::string> protos) {
		if (protos.size() != 2)
			std::cout << "Bad protocol size\n";
		if (protos[0] != "test/1" || protos[1] != "test/2")
			std::cout << "Bad protocol values\n";
		return "test/3";
	};

	TLS::Client client(
		[&](const byte buf[], size_t sz) { c2s_q.insert(c2s_q.end(), buf, buf + sz); },
		save_client_data,
		print_alert,
		handshake_complete,
		client_sessions,
		creds,
		policy,
		rng,
		TLS::Server_Information(),
		offer_version,
		next_protocol_chooser);

	while (true) {
		if (client.is_active())
			client.send("1");

		if (server.is_active()) {
			if (server.next_protocol() != "test/3")
				std::cout << "Wrong protocol " << server.next_protocol() << "\n";
			server.send("2");
		}

		/*
		* Use this as a temp value to hold the queues as otherwise they
		* might end up appending more in response to messages during the
		* handshake.
		*/
		std::vector<byte> input;
		std::swap(c2s_q, input);

		try {
			server.received_data(&input[0], input.size());
		} catch(std::exception & e) {
			std::cout << "Server error - " << e.what() << "\n";
			break;
		}

		input.clear();
		std::swap(s2c_q, input);

		try {
			client.received_data(&input[0], input.size());
		} catch(std::exception & e) {
			std::cout << "Client error - " << e.what() << "\n";
			break;
		}

		if (c2s_data.size()) {
			if (c2s_data[0] != '1') {
				std::cout << "Error\n";
				return false;
			}
		}

		if (s2c_data.size()) {
			if (s2c_data[0] != '2') {
				std::cout << "Error\n";
				return false;
			}
		}

		if (s2c_data.size() && c2s_data.size()) break;
	}

	return true;
}

class Test_Policy : public TLS::Policy
{
public:
	bool acceptable_protocol_version(TLS::Protocol_Version) const { return true; }
	bool send_fallback_scsv(TLS::Protocol_Version) const { return false; }
};

}

class TLS_test : public QObject
{
	Q_OBJECT
private slots:

	void initTestCase()
	{
		QVERIFY(initCrypto());
	}

	void test_tls()
	{
		Test_Policy default_policy;
		AutoSeeded_RNG rng;

		std::auto_ptr<Credentials_Manager> basic_creds(create_creds(rng));

		QVERIFY(test_handshake(rng, TLS::Protocol_Version::TLS_V10, *basic_creds, default_policy));
		QVERIFY(test_handshake(rng, TLS::Protocol_Version::TLS_V11, *basic_creds, default_policy));
		QVERIFY(test_handshake(rng, TLS::Protocol_Version::TLS_V12, *basic_creds, default_policy));
	}

};
#include "tls_test.moc"
ADD_TEST(TLS_test)
