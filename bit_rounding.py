import numpy as np
import numba
from numba import jit, types
import math

# Constants
NBITS = 32
FLOAT_SIGN_MASK = 0x80000000
FLOAT_SIGNIFICAND_MASK = 0x007fffff
FLOAT_EXPONENT_MASK = 0x7f800000
FLOAT_SIGNIFICAND_BITS = 23
FLOAT_EXPONENT_BIAS = 127
BIT_XPL_NBR_SGN_FLT = 23

@jit(nopython=True)
def normal_inv_acklam(p):
    """Acklam's inverse normal CDF approximation"""
    # Acklam coefficients
    a = np.array([-3.969683028665376e+01, 2.209460984245205e+02,
                  -2.759285104469687e+02, 1.383577518672690e+02,
                  -3.066479806614716e+01, 2.506628277459239e+00])
    b = np.array([-5.447609879822406e+01, 1.615858368580409e+02,
                  -1.556989798598866e+02, 6.680131188771972e+01,
                  -1.328068155288572e+01])
    c = np.array([-7.784894002430293e-03, -3.223964580411365e-01,
                  -2.400758277161838e+00, -2.549732539343734e+00,
                   4.374664141464968e+00,  2.938163982698783e+00])
    d = np.array([ 7.784695709041462e-03,  3.224671290700398e-01,
                   2.445134137142996e+00,  3.754408661907416e+00])

    if p <= 0.0:
        return -np.inf
    if p >= 1.0:
        return np.inf

    # break-points
    p_low = 0.02425
    p_high = 1.0 - p_low

    # lower region
    if p < p_low:
        q = math.sqrt(-2.0 * math.log(p))
        return (((((c[0]*q + c[1])*q + c[2])*q + c[3])*q + c[4])*q + c[5]) / \
               ((((d[0]*q + d[1])*q + d[2])*q + d[3])*q + 1.0)

    # upper region
    if p > p_high:
        q = math.sqrt(-2.0 * math.log(1.0 - p))
        return -(((((c[0]*q + c[1])*q + c[2])*q + c[3])*q + c[4])*q + c[5]) / \
                ((((d[0]*q + d[1])*q + d[2])*q + d[3])*q + 1.0)

    # central region
    q = p - 0.5
    r = q*q
    return (((((a[0]*r + a[1])*r + a[2])*r + a[3])*r + a[4])*r + a[5]) * q / \
           (((((b[0]*r + b[1])*r + b[2])*r + b[3])*r + b[4])*r + 1.0)

@jit(nopython=True)
def binom_confidence(n, c):
    """Calculate binomial confidence"""
    v = 1.0 - (1.0 - c) * 0.5
    p = 0.5 + normal_inv_acklam(v) / (2.0 * math.sqrt(n))
    return 1.0 if p > 1.0 else p

@jit(nopython=True)
def entropy2(p1, p2):
    """Calculate binary entropy"""
    result = 0.0
    if p1 > 0.0:
        result -= p1 * math.log(p1)
    if p2 > 0.0:
        result -= p2 * math.log(p2)
    return result / math.log(2.0)

@jit(nopython=True)
def binom_free_entropy(n, c):
    """Calculate binomial free entropy"""
    p = binom_confidence(n, c)
    return 1.0 - entropy2(p, 1.0 - p)

@jit(nopython=True)
def signed_exponent_kernel(a_uint):
    """Convert float to signed exponent representation"""
    sfmask = FLOAT_SIGN_MASK | FLOAT_SIGNIFICAND_MASK
    emask = FLOAT_EXPONENT_MASK
    esignmask = FLOAT_SIGN_MASK >> 1
    
    sbits = FLOAT_SIGNIFICAND_BITS
    bias = FLOAT_EXPONENT_BIAS
    
    ui = a_uint
    sf = ui & sfmask
    e = int((ui & emask) >> sbits) - bias
    eabs = abs(e)
    esign = esignmask if e < 0 else 0
    esigned = esign | (eabs << sbits)
    
    return sf | esigned

@jit(nopython=True)
def signed_exponent(data):
    """Apply signed exponent transformation to array"""
    result = np.empty_like(data, dtype=np.float32)
    # View as uint32 for bit manipulation
    data_uint = data.view(np.uint32)
    result_uint = result.view(np.uint32)
    
    for i in range(len(data)):
        result_uint[i] = signed_exponent_kernel(data_uint[i])
    
    return result

@jit(nopython=True)
def bitpair_count_kernel(a, b, BC):
    """Count bit pairs between two uint32 values"""
    mask = 1
    for i in range(NBITS):
        j = (a & mask) >> i
        k = (b & mask) >> i
        BC[NBITS - i - 1, j, k] += 1
        mask <<= 1

@jit(nopython=True)
def bitpair_count(A, B):
    """Count bit pairs between two arrays"""
    n = len(A)
    BC = np.zeros((NBITS, 2, 2), dtype=np.int32)
    
    A_uint = A.view(np.uint32)
    B_uint = B.view(np.uint32)
    
    for i in range(n):
        bitpair_count_kernel(A_uint[i], B_uint[i], BC)
    
    return BC

@jit(nopython=True)
def mutual_information_kernel(p):
    """Calculate mutual information from probability matrix"""
    py = np.array([p[0,0] + p[1,0], p[0,1] + p[1,1]])
    px = np.array([p[0,0] + p[0,1], p[1,0] + p[1,1]])
    
    M = 0.0
    for j in range(2):
        for i in range(2):
            if p[i,j] > 0.0:
                M += p[i,j] * math.log(p[i,j] / px[i] / py[j])
    
    return M / math.log(2.0)

@jit(nopython=True)
def mutual_information(A, B, nelements):
    """Calculate mutual information between two arrays"""
    confidence = 0.99
    BC = bitpair_count(A, B)
    
    MI = np.zeros(NBITS)
    P = np.zeros((2, 2))
    
    for i in range(NBITS):
        for j in range(2):
            for k in range(2):
                P[j,k] = BC[i,j,k] / nelements
        MI[i] = mutual_information_kernel(P)
    
    # Set zero insignificant values
    Hfree = binom_free_entropy(nelements, confidence)
    for i in range(NBITS):
        if MI[i] <= Hfree:
            MI[i] = 0.0
    
    return MI

@jit(nopython=True)
def bitinformation(data):
    """Calculate bit information for array"""
    n = len(data)
    if n < 2:
        return np.zeros(NBITS)
    return mutual_information(data, data[1:], n - 1)

@jit(nopython=True)
def get_keepbits(bit_info, inflevel):
    """Get number of bits to keep based on information level"""
    floatNMBITS = 9
    keepMantissaBits = 23
    
    # Find maximum bit info
    bitInfoMax = -1e33
    for i in range(NBITS):
        if bit_info[i] > bitInfoMax:
            bitInfoMax = bit_info[i]
    
    # Find maximum in last 4 bits
    bitInfoMaxLast4 = -1e33
    for i in range(NBITS - 4, NBITS):
        if bit_info[i] > bitInfoMaxLast4:
            bitInfoMaxLast4 = bit_info[i]
    bitInfoMaxLast4 *= 1.5
    
    # Clean bit info
    infoPerBitCleaned = np.zeros(NBITS)
    for i in range(NBITS):
        if bit_info[i] > bitInfoMaxLast4:
            infoPerBitCleaned[i] = bit_info[i]
    
    # Calculate cumulative sum
    for i in range(1, NBITS):
        infoPerBitCleaned[i] += infoPerBitCleaned[i - 1]
    
    lastBit = infoPerBitCleaned[NBITS - 1]
    if lastBit > 0.0:
        # Calculate CDF
        cdf = infoPerBitCleaned / lastBit
        
        nonMantissaBits = floatNMBITS
        
        for i in range(NBITS):
            if cdf[i] > inflevel:
                keepMantissaBits = i + 1 - nonMantissaBits
                break
    
    nsb = keepMantissaBits
    if nsb < 1:
        nsb = 1
    if nsb > 23:
        nsb = 23
    
    return nsb

@jit(nopython=True)
def get_keepbits_gradient(bit_info, threshold, tolerance):
    """
    Get number of bits to keep using gradient-based method with artificial information removal.
    
    This function calculates a modified CDF for bit information and removes artificial information
    by identifying where the gradient becomes close to the tolerance while maintaining the 
    threshold of cumulative information content.
    
    Parameters:
    -----------
    bit_info : numpy.ndarray
        Information content of each bit
    threshold : float  
        Minimum cumulative sum of information content before artificial information filter is applied
    tolerance : float
        The tolerance value below which gradient starts becoming constant
        
    Returns:
    --------
    int
        Number of bits to keep
    """
    floatNMBITS = 9
    keepMantissaBits = 23
    
    # Clean bit info
    infoPerBitCleaned = np.zeros(NBITS)
    for i in range(NBITS):
        if bit_info[i] >= 0:
            infoPerBitCleaned[i] = bit_info[i]
    
    # Calculate cumulative sum
    for i in range(1, NBITS):
        infoPerBitCleaned[i] += infoPerBitCleaned[i - 1]
    
    lastBit = infoPerBitCleaned[NBITS - 1]
    if lastBit > 0.0:
        # Calculate CDF
        cdf = infoPerBitCleaned / lastBit
        
        # Calculate gradient of CDF
        gradient_array = np.zeros(NBITS - 1)
        for i in range(NBITS - 1):
            gradient_array[i] = cdf[i + 1] - cdf[i]
        
        # Total sum of information
        infSum = 0.0
        for i in range(NBITS):
            infSum += bit_info[i]
        
        # Sign and exponent bits (assuming 32-bit float: 1 sign + 8 exponent = 9 bits)
        sign_and_exponent = floatNMBITS
        
        # Sum of sign and exponent bits
        SignExpSum = 0.0
        for i in range(sign_and_exponent):
            SignExpSum += bit_info[i]
        
        # Initialize current bit sum with sign and exponent sum
        CurrentBit_Sum = SignExpSum
        infbits = NBITS - 1  # Default to all bits
        
        # Find intersection point where gradient < tolerance and cumulative sum >= threshold * infSum
        for i in range(sign_and_exponent, len(gradient_array) - 1):
            CurrentBit_Sum += bit_info[i]
            if gradient_array[i] < tolerance and CurrentBit_Sum >= threshold * infSum:
                infbits = i
                break
        
        # Calculate keep bits based on infbits
        keepMantissaBits = infbits + 1 - floatNMBITS
    
    # Ensure valid range
    nsb = keepMantissaBits
    if nsb < 1:
        nsb = 1
    if nsb > 23:
        nsb = 23
    
    return nsb

@jit(nopython=True)
def get_keepbits_monotonic(bit_info, inflevel):
    """Get number of bits to keep based on information level
    When calculating CDF, it uses the monotonic component of
    exponential moving averaged bit information.
    """
    floatNMBITS = 9
    keepMantissaBits = 23
    
    # Clean bit info
    infoPerBitCleaned = np.zeros(NBITS)
    infoCDF = np.zeros(NBITS)
    flag = 0
    current_min = bit_info[floatNMBITS]
    for i in range(NBITS):
        if i < floatNMBITS:
            infoPerBitCleaned[i] = bit_info[i]
            continue
        current_min = min(current_min, bit_info[i])
        if bit_info[i] > current_min * 1.5:
            flag += 1
        infoPerBitCleaned[i] = 0 if flag > 2 else bit_info[i]
    

    # Calculate cumulative sum
    infoCDF[0] = infoPerBitCleaned[0]
    for i in range(1, NBITS):
        infoCDF[i] = infoPerBitCleaned[i] + infoCDF[i - 1]
    
    lastBit = infoCDF[NBITS - 1]
    if lastBit > 0.0:
    # Calculate CDF
        cdf = infoCDF / lastBit
    else:
        cdf = infoCDF

    nonMantissaBits = floatNMBITS
    
    for i in range(NBITS):
        if cdf[i] > inflevel:
            keepMantissaBits = i + 1 - nonMantissaBits
            break
    
    nsb = keepMantissaBits
    if nsb < 1:
        nsb = 1
    if nsb > 23:
        nsb = 23

    return nsb

@jit(nopython=True)
def analyze_and_get_nsb(data, inflevel, monotonic=False):
    """Analyze data and get number of significant bits"""
    if len(data) < 2:
        return 1
    
    # Create copy and apply signed exponent
    v_copy = signed_exponent(data.copy())
    
    # Calculate bit information
    bit_info = bitinformation(v_copy)
    
    # Get number of bits to keep
    if monotonic:
        nsb = get_keepbits_monotonic(bit_info, inflevel)
    else:
        nsb = get_keepbits(bit_info, inflevel)
        # Use gradient-based method
        #nsb = get_keepbits_gradient(bit_info, inflevel, tolerance=0.01)
    
    return nsb

@jit(nopython=True)
def bitround(nsb, data, missval):
    """Apply bit rounding to data array"""
    prc_bnr_xpl_rqr = nsb
    bit_xpl_nbr_zro = BIT_XPL_NBR_SGN_FLT - prc_bnr_xpl_rqr
    
    msk_f32_u32_zro = 0xffffffff
    msk_f32_u32_zro <<= bit_xpl_nbr_zro
    
    msk_f32_u32_one = ~msk_f32_u32_zro
    msk_f32_u32_hshv = msk_f32_u32_one & (msk_f32_u32_zro >> 1)
    
    # Work with uint32 view for bit manipulation
    u32_ptr = data.view(np.uint32)
    
    for idx in range(len(data)):
        if data[idx] != missval and not math.isnan(data[idx]):
            u32_ptr[idx] += msk_f32_u32_hshv
            u32_ptr[idx] &= msk_f32_u32_zro