module.exports = {
  compilers: {
    solc: {
      version: "^0.8.4",
      settings: {
        optimizer: {
          enabled: true,
          runs: 100
        }
      }
    }
  },
  plugins: ["solidity-coverage"]
};
