/*
* NIST elliptic curves over GF(p)
* (C) 2014 Jack Lloyd
*
* Botan is released under the Simplified BSD License (see license.txt)
*/

#ifndef BOTAN_GFP_CURVE_NIST_H__
#define BOTAN_GFP_CURVE_NIST_H__

#include <botan/curve_gfp.h>
#include <memory>

namespace Botan {

class CurveGFp_NIST : public CurveGFp_Repr
   {
   public:
      CurveGFp_NIST(size_t p_bits, const BigInt& a, const BigInt& b) :
         m_a(a), m_b(b), m_p_words((p_bits + BOTAN_MP_WORD_BITS - 1) / BOTAN_MP_WORD_BITS)
         {
         }

      size_t get_p_words() const override { return m_p_words; }

      const BigInt& get_a() const override { return m_a; }

      const BigInt& get_b() const override { return m_b; }

      const BigInt& get_a_rep() const override { return m_a; }

      const BigInt& get_b_rep() const override { return m_b; }

      void to_curve_rep(BigInt& x, secure_vector<word>& ws) const override
         { redc(x, ws); }

      void from_curve_rep(BigInt& x, secure_vector<word>& ws) const override
         { redc(x, ws); }

      void curve_mul(BigInt& z, const BigInt& x, const BigInt& y,
                     secure_vector<word>& ws) const override;

      void curve_sqr(BigInt& z, const BigInt& x,
                     secure_vector<word>& ws) const override;
   private:
      virtual void redc(BigInt& x, secure_vector<word>& ws) const = 0;

      virtual size_t max_redc_subtractions() const = 0;

      // Curve parameters
      BigInt m_a, m_b;
      size_t m_p_words; // cache of m_p.sig_words()
   };

/**
* The NIST P-521 curve
*/
class CurveGFp_P521 : public CurveGFp_NIST
   {
   public:
      CurveGFp_P521(const BigInt& a, const BigInt& b) : CurveGFp_NIST(521, a, b) {}

      static const BigInt& prime();

      const BigInt& get_p() const override { return CurveGFp_P521::prime(); }

   private:
      void redc(BigInt& x, secure_vector<word>& ws) const override;

      size_t max_redc_subtractions() const override { return 1; }
   };

}

#endif
